# Adds (or removes) a menu-selectable zone in zonelist.lua. The zone name is
# also the world folder name, lowercased: zone RacePipe_TopX8 loads
# assets/worlds/racepipe_topx8/. Extracted from add-sumo.ps1, which does this
# plus world/texture placement; this is just the menu step.
#
# Requires startup2.zip already disabled and the loose tree in place
# (add-sumo.ps1 does that and it is still done in this install).

param(
  [Parameter(Mandatory=$true)][string]$Zone,
  # Real zones inherit a parent carrying MasterZone/PlaysetProperties/System/
  # SaveZoneOverride. A hand-authored record without one gets no environment set
  # up, which is why every world loaded through it - including stock 3.0
  # geometry - rendered unlit.
  [string]$Inherits,
  [switch]$Revert,
  [string]$Game = 'D:\SteamLibrary\steamapps\common\Disney Infinity 3.0 Gold Edition'
)

$ErrorActionPreference = 'Stop'
$zl = Join-Path $Game 'assets\gamedb\core\zonelist.lua'
$t  = [char]9
$text = [IO.File]::ReadAllText($zl)
$lines = $text -split "`n"

if ($Revert) {
  # the record is a variable number of lines now, so skip to its closing brace
  $out = New-Object Collections.Generic.List[string]
  $inRec = $false
  foreach ($line in $lines) {
    if (-not $inRec -and $line -match "^\t{3}$Zone = \s*$") { $inRec = $true; continue }
    if ($inRec) { if ($line -match '^\t{3}\},\s*$') { $inRec = $false }; continue }
    [void]$out.Add($line)
  }
  [IO.File]::WriteAllText($zl, ($out -join "`n"), (New-Object Text.UTF8Encoding($false)))
  "removed zone $Zone"; return
}

if ($text -match "(?m)^\t{3}$Zone = ") { throw "$Zone already present" }

$record = @("$t$t$t$Zone = ", "$t$t$t{")
if (-not $Inherits) {
  $record += "$t$t$t${t}MasterZone = `"PSX_Master`","
  $record += "$t$t$t${t}PlaysetProperties = `"PlaysetX`","
}
$record += "$t$t$t${t}SplitScreenAllowed = false,"
$record += "$t$t$t${t}_ShowInMenu_ = true,"
$record += "$t$t$t${t}availableInConsumerBuild = true,"
$record += "$t$t$t${t}availableInShippingBuild = true,"
if ($Inherits) { $record += "$t$t$t${t}_Inherits_ = `"$Inherits`"," }
$record += "$t$t$t},"

$out = New-Object Collections.Generic.List[string]
$done = $false
foreach ($line in $lines) {
  if (-not $done -and $line -match '^\t{3}PSX_Intro = \s*$') {
    foreach ($r in $record) { [void]$out.Add($r) }
    $done = $true
  }
  [void]$out.Add($line)
}
if (-not $done) { throw 'PSX_Intro anchor not found' }
[IO.File]::WriteAllText($zl, ($out -join "`n"), (New-Object Text.UTF8Encoding($false)))
"added zone $Zone -> assets/worlds/$($Zone.ToLower())/"
