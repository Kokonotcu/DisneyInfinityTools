# Unattended in-game test: boot 3.0, travel to a zone with a character, run Lua probes, screenshot, quit.
#   powershell -File harness.ps1 [-Zone OUT_Intro2] [-Sku 1000206] [-Probe a.lua,b.lua] [-Keep] [-Attach]
# Needs CrabeLoader (bink2w32.dll proxy, built from source with its engine guards off): it runs
# <game>\crabe_remote_cmd.txt in the game Lua state and logs the result to loader.log as '= ...' / '! ...'.
# Commands only run while the game itself calls Lua (CrabeLoader ticks from its lua_pcall hook): an idle menu
# such as the title screen never does, so screens that wait for a press are pressed from their own MovieLoaded.
# Boot flow found 2026-10-12 (each step was needed):
#   autosave notice  waits for a tap         -> autoSave_MainMenu<N>:AutoSaveConfirmed()
#   title screen     waits for Start; idle   -> PressStart MovieLoaded hook: onKeyDown{action=START_GAME}
#   (screen instances are globals named <Class><counter>; classes appear when their screen is required)
#   main menu (LevelSelectMenu<N>): profile and the 104-character roster are loaded
#   UI_LaunchLevel(level, "world", true, true, false) opens the Virtual Reader (character select);
#   VirtualReaderPC_SetCurrentCharacter(sku) + VirtualReaderPC_ActivateChanges(0, true), then
#   UI_LaunchLevel(..., characterSelected = true) loads the world with that avatar (FUN_00b81350 arg 6).
#   Without the title-screen press there is no profile: the world loads with no avatar (camera at the origin).
param([string]$Zone = 'OUT_Intro2', [int]$Sku = 1000206, [string[]]$Probe = @(),
      [switch]$Keep, [switch]$Attach, [int]$BootTimeout = 300, [int]$LoadTimeout = 180,
      [string]$Game = 'D:\SteamLibrary\steamapps\common\Disney Infinity 3.0 Gold Edition',
      [string]$Out = "$env:TEMP\di3harness")
$ErrorActionPreference = 'Stop'
$cmd = Join-Path $Game 'crabe_remote_cmd.txt'; $loader = Join-Path $Game 'loader.log'; $pent = Join-Path $Game 'pentane.log'
New-Item -ItemType Directory -Force $Out | Out-Null

function Lines($f) { if (Test-Path $f) { @(Get-Content $f) } else { @() } }
function Alive { [bool](Get-Process DisneyInfinity3 -ErrorAction SilentlyContinue) }

# Runs one Lua chunk; returns its '= ...' / '! ...' result, or $null on timeout.
function Lua([string]$code, [int]$timeout = 15) {
    $n = (Lines $loader).Count
    # write then rename: the loader polls 4x/s and deletes an empty (half-written) file unrun;
    # Set-Content -NoNewline with a positional value writes nothing in PowerShell 5.1
    $tmp = Join-Path $Game 'di3harness_cmd.tmp'
    [IO.File]::WriteAllText($tmp, $code)
    Move-Item -Force $tmp $cmd
    $end = (Get-Date).AddSeconds($timeout)
    while ((Get-Date) -lt $end -and (Alive)) {
        Start-Sleep -Milliseconds 400
        $r = Lines $loader | Select-Object -Skip $n | Where-Object { $_ -match '^\[\d{4}-[^\]]*\] \[\w+\] [=!] ' } | Select-Object -First 1
        if ($r) { return ($r -replace '^\[[^\]]*\] \[\w+\] ', '') }
    }
    Remove-Item $cmd -ErrorAction SilentlyContinue
    return $null
}

# Polls a Lua chunk until its result matches $want.
function Until([string]$code, [string]$want, [int]$timeout) {
    $end = (Get-Date).AddSeconds($timeout); $r = $null
    while ((Get-Date) -lt $end -and (Alive)) { $r = Lua $code 6; if ($r -match $want) { return $r }; Start-Sleep 2 }
    return $r
}

function Shot([string]$name) {
    Add-Type -AssemblyName System.Windows.Forms, System.Drawing
    $b = [System.Windows.Forms.SystemInformation]::VirtualScreen
    $bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
    [System.Drawing.Graphics]::FromImage($bmp).CopyFromScreen($b.Left, $b.Top, 0, 0, $bmp.Size)
    $f = Join-Path $Out "$name.png"; $bmp.Save($f, [System.Drawing.Imaging.ImageFormat]::Png); $f
}

$BOOT = @'
_G.__harness_log = _G.__harness_log or {}
if not _G.__harness_require then
  local auto = { PressStart = function(self) return self:onKeyDown({ action = KeyMap.START_GAME, controllerId = 0, playerNum = 0, keyDown = true }) end }
  local origRequire = require
  _G.__harness_require = origRequire
  require = function(name, ...)
    local res = origRequire(name, ...)
    for cls, act in pairs(auto) do
      local C = rawget(_G, cls)
      if type(C) == "table" and not rawget(C, "__harness_patched") then
        rawset(C, "__harness_patched", true)
        local ml = C.MovieLoaded
        C.MovieLoaded = function(self, ...)
          local r = { ml(self, ...) }
          local ok, err = pcall(act, self)
          __harness_log[#__harness_log + 1] = cls .. " pressed: " .. tostring(ok) .. " " .. tostring(err)
          return unpack(r)
        end
      end
    end
    return res
  end
end
for k, v in pairs(_G) do
  if type(k) == "string" and k:match("^autoSave_MainMenu%d+$") and type(v) == "table" then
    local ok, err = pcall(v.AutoSaveConfirmed, v)
    return "confirmed " .. k .. " " .. tostring(ok) .. " " .. tostring(err)
  end
end
return "waiting for the autosave notice"
'@
$MENU = 'for k in pairs(_G) do if type(k) == "string" and k:match("^LevelSelectMenu%d+$") then return "menu " .. k end end return "not yet"'
$OPENVR = "Crabe.native('UI_LaunchLevel', 'harness')('$Zone', 'world', true, true, false); return 'vr'"
$VRUP = 'for k in pairs(_G) do if type(k) == "string" and k:match("^VirtualReaderPC_Loadout%d+$") then return "vr " .. k end end return "not yet"'
$SETSKU = "VirtualReaderPC_SetCurrentCharacter($Sku); VirtualReaderPC_ActivateChanges(0, true); return 'sku ' .. tostring(VirtualReader_GetCurrentAvatarSku())"
$LAUNCH = "Crabe.native('UI_LaunchLevel', 'harness')('$Zone', 'world', true, true, false, true); return 'launched'"
$POS = 'local h = Game.GetAvatarHandle(); if not h or h == 0 then return "noavatar" end; local x, y, z = Game.ActorPosition(h); return string.format("avatar %s | %s | %.2f,%.2f,%.2f", tostring(Game.CurrentWorld()), tostring(Game.CurrentZone()), x, y, z)'

Remove-Item $cmd -ErrorAction SilentlyContinue
$t0 = Get-Date
$n0 = (Lines $loader).Count          # loader.log is appended across runs: only lines after n0 count
if (-not $Attach) {
    if (Alive) { Stop-Process -Name DisneyInfinity3 -Force; Start-Sleep 5 }
    Start-Process 'steam://rungameid/541670'      # a relaunch right after a kill can take Steam ~3 min
    $end = $t0.AddSeconds($BootTimeout)
    while ((Get-Date) -lt $end -and -not (Lines $loader | Select-Object -Skip $n0 | Where-Object { $_ -match 'Embedded API loaded' })) { Start-Sleep 2 }
    "api: $([int]((Get-Date) - $t0).TotalSeconds)s"
    "boot: " + (Until $BOOT '^= confirmed' 60)
}
$menu = Until $MENU '^= menu' 90
"menu: $menu ($([int]((Get-Date) - $t0).TotalSeconds)s)"
if ($menu -notmatch '^= menu') { 'FAIL: main menu not reached'; exit 1 }
"openvr: " + (Lua $OPENVR)
"vr: " + (Until $VRUP '^= vr' 30)
"sku: " + (Lua $SETSKU)
$tl = Get-Date
"launch: " + (Lua $LAUNCH)
$state = Until $POS '^= avatar' $LoadTimeout
"world: $state ($([int]((Get-Date) - $tl).TotalSeconds)s after launch)"
Start-Sleep 3
foreach ($p in $Probe) { "probe $p : " + (Lua (Get-Content -Raw $p) 60) }
"screenshot: " + (Shot "$Zone-$(Get-Date -Format HHmmss)")

$exc = @(Select-String -Path $pent -Pattern 'Exception Caught' -ErrorAction SilentlyContinue)
"pentane exceptions: $($exc.Count)"; $exc | Select-Object -Last 5 | ForEach-Object { '  ' + $_.Line }
Get-ChildItem (Join-Path $Game 'Crabe') -Filter 'crash-*' -ErrorAction SilentlyContinue |
    Where-Object { $_.LastWriteTime -gt $t0 } | ForEach-Object { "crash report: $($_.FullName)" }
if (-not $Keep) { Lua 'Game.QuitGame(); return "quit"' 5 | Out-Null; Start-Sleep 8; if (Alive) { Stop-Process -Name DisneyInfinity3 -Force } }
if ($state -notmatch '^= avatar') { exit 2 }
