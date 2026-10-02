# Makes an existing zonelist.lua record menu-selectable, leaving its parent,
# MasterZone and realm wiring exactly as Avalanche wrote them. add-zone.ps1
# authors a brand-new record; this one only opens a shipping zone.
#
# The menu gate is THREE flags, evaluated after _Inherits_ resolution:
#     _ShowInMenu_ = true
#     availableInConsumerBuild = true
#     availableInShippingBuild = true
# A parent may supply some and not others. Inherit_PSX_Arcade_Parent, for
# instance, carries _ShowInMenu_ and availableInConsumerBuild but no
# availableInShippingBuild, so every zone under it stays invisible in the retail
# Gold build no matter what the child sets. Setting only _ShowInMenu_ is the
# mistake that costs a launch. Check with: zone-info.ps1 -Zone <name>
#
# The zone name lowercased is the world folder: PSX_Arcade_01 loads
# assets/worlds/psx_arcade_01/.

param(
  [Parameter(Mandatory=$true)][string]$Zone,
  [switch]$Revert,
  [string]$Game = 'D:\SteamLibrary\steamapps\common\Disney Infinity 3.0 Gold Edition'
)

$ErrorActionPreference = 'Stop'
$zl = Join-Path $Game 'assets\gamedb\core\zonelist.lua'
$t  = [char]9
$lines = [Collections.Generic.List[string]][IO.File]::ReadAllText($zl).Split("`n")

$flags = 'availableInShippingBuild','availableInConsumerBuild','_ShowInMenu_'   # inserted in reverse

$start = -1
for ($i = 0; $i -lt $lines.Count; $i++) { if ($lines[$i] -match "^$t{3}$([regex]::Escape($Zone)) = \s*$") { $start = $i; break } }
if ($start -lt 0) { throw "$Zone not found in zonelist.lua" }

$end = -1
for ($i = $start + 1; $i -lt $lines.Count; $i++) { if ($lines[$i].Trim() -eq '},') { $end = $i; break } }
if ($end -lt 0) { throw "$Zone record not terminated" }

if ($Revert) {
  $removed = 0
  for ($i = $end - 1; $i -gt $start; $i--) {
    foreach ($f in $flags) { if ($lines[$i] -match "^$t{4}$f = ") { $lines.RemoveAt($i); $removed++; break } }
  }
  [IO.File]::WriteAllText($zl, ($lines -join "`n"), (New-Object Text.UTF8Encoding($false)))
  "removed $removed menu flag(s) from $Zone"; return
}

$added = @()
foreach ($f in $flags) {
  $has = $false
  for ($i = $start; $i -lt $end; $i++) { if ($lines[$i] -match "^$t{4}$f = ") { $has = $true; break } }
  if ($has) { continue }
  $lines.Insert($start + 2, "$t$t$t${t}$f = true,")
  $added += $f
  $end++
}
[IO.File]::WriteAllText($zl, ($lines -join "`n"), (New-Object Text.UTF8Encoding($false)))
if ($added.Count) { "$Zone : added $($added -join ', ') -> assets/worlds/$($Zone.ToLower())/" }
else { "$Zone already carried all three flags in its own record" }
