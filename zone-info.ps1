# Parses zonelist.lua into zone records and resolves _Inherits_ chains, so the
# effective value of a field can be read the way the game sees it.
#
# Written because a zone that looked menu-ready (_ShowInMenu_ = true via its
# parent) never appeared: the visible set is gated on more than one flag, and
# the flags arrive from different levels of the inheritance chain.

param(
  [string]$Zone,                 # dump one zone's effective fields
  [switch]$Menu,                 # list every zone the menu gate admits
  [string]$Filter,               # restrict -Menu to one UIPlaysetFilter/PlaysetProperties
  [string]$Game = 'D:\SteamLibrary\steamapps\common\Disney Infinity 3.0 Gold Edition'
)

$ErrorActionPreference = 'Stop'
$zl = Join-Path $Game 'assets\gamedb\core\zonelist.lua'
$lines = [IO.File]::ReadAllLines($zl)
$t = [char]9

# --- parse: records are at 3 tabs, scalar fields at 4 -------------------------
$rec = @{}
$order = New-Object Collections.Generic.List[string]
$cur = $null
foreach ($ln in $lines) {
  if ($ln -match "^$t{3}([A-Za-z0-9_]+) = \s*$") { $cur = $Matches[1]; $rec[$cur] = @{}; [void]$order.Add($cur); continue }
  if ($null -eq $cur) { continue }
  if ($ln.Trim() -eq '},') { $cur = $null; continue }
  if ($ln -match "^$t{4}([A-Za-z0-9_]+) = (.+?),\s*$") {
    $v = $Matches[2].Trim()
    if ($v -notmatch '^\{') { $rec[$cur][$Matches[1]] = ($v -replace '^"|"$','') }
  }
}

function Eff($name, $seen = @()) {
  if (-not $rec.ContainsKey($name) -or $seen -contains $name) { return @{} }
  $own = $rec[$name]
  $out = @{}
  if ($own.ContainsKey('_Inherits_')) {
    foreach ($kv in (Eff $own['_Inherits_'] ($seen + $name)).GetEnumerator()) { $out[$kv.Key] = $kv.Value }
  }
  foreach ($kv in $own.GetEnumerator()) { $out[$kv.Key] = $kv.Value }   # own fields win
  $out
}

# The three flags every zone that has ever appeared in the menu carries.
function Visible($e) {
  ($e['_ShowInMenu_'] -eq 'true') -and
  ($e['availableInConsumerBuild'] -eq 'true') -and
  ($e['availableInShippingBuild'] -eq 'true')
}

if ($Zone) {
  if (-not $rec.ContainsKey($Zone)) { throw "$Zone not found" }
  $e = Eff $Zone
  Write-Output "== $Zone  (own fields: $($rec[$Zone].Keys.Count), effective: $($e.Keys.Count))"
  foreach ($k in ($e.Keys | Sort-Object)) {
    $src = if ($rec[$Zone].ContainsKey($k)) { 'own' } else { 'inherited' }
    Write-Output ("  {0,-30} = {1,-28} [{2}]" -f $k, $e[$k], $src)
  }
  Write-Output ("  --> menu-visible: {0}" -f (Visible $e))
  return
}

if ($Menu) {
  $n = 0
  foreach ($z in $order) {
    if ($z -like 'Inherit_*') { continue }
    $e = Eff $z
    if (-not (Visible $e)) { continue }
    if ($Filter -and $e['PlaysetProperties'] -ne $Filter) { continue }
    $n++
    Write-Output ("{0,-34} playset={1,-12} master={2}" -f $z, $e['PlaysetProperties'], $e['MasterZone'])
  }
  Write-Output "-- $n menu-visible zones"
  return
}

Write-Output ("parsed {0} records from zonelist.lua; use -Zone <name> or -Menu" -f $rec.Count)
