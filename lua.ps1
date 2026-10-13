# Runs one Lua chunk in the running game (CrabeLoader remote command) and prints its result line.
#   powershell -File lua.ps1 -Code 'return Game.CurrentWorld()'      or   -File probe.lua
param([string]$Code, [string]$File, [int]$Timeout = 20,
      [string]$Game = 'D:\SteamLibrary\steamapps\common\Disney Infinity 3.0 Gold Edition')
if ($File) { $Code = Get-Content -Raw $File }
$cmd = Join-Path $Game 'crabe_remote_cmd.txt'; $loader = Join-Path $Game 'loader.log'
$n = @(Get-Content $loader).Count
# write then rename: the loader polls 4x/s and deletes an empty (half-written) file unrun;
# Set-Content -NoNewline with a positional value writes nothing in PowerShell 5.1
$tmp = Join-Path $Game 'di3harness_cmd.tmp'
[IO.File]::WriteAllText($tmp, $Code)
Move-Item -Force $tmp $cmd
$end = (Get-Date).AddSeconds($Timeout)
while ((Get-Date) -lt $end) {
    Start-Sleep -Milliseconds 300
    $r = @(Get-Content $loader) | Select-Object -Skip $n | Where-Object { $_ -match '^\[\d{4}-[^\]]*\] \[\w+\] [=!] ' } | Select-Object -First 1
    if ($r) { $r -replace '^\[[^\]]*\] \[\w+\] ', ''; exit 0 }
}
Remove-Item $cmd -ErrorAction SilentlyContinue
'TIMEOUT'; exit 1
