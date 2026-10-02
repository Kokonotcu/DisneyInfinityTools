# Decodes a .btd collision mesh header and prints its extent, so the collision
# actually in play can be compared against the geometry being rendered.
#
# Header is u32[]: [0] vertex offset, [4] vertex count, [5] triangle count.
# Positions are f32x3 from the vertex offset, indices u16 after them, and a
# padded AABB sits at +80 carrying a 1.0 margin. The block at +320 is undecoded
# and looks like a BVH. Sibling .btm is a 16-byte header whose version word is
# C001 in 1.0 and C007 in 3.0.

param([Parameter(Mandatory=$true)][string[]]$Path)

foreach ($p in $Path) {
  if (-not (Test-Path $p)) { Write-Output "$p : missing"; continue }
  $b = [IO.File]::ReadAllBytes($p)
  $name = Split-Path $p -Leaf
  if ($b.Length -lt 96) { Write-Output ("{0,-34} {1} B - too small to decode" -f $name,$b.Length); continue }
  $vOff = [BitConverter]::ToUInt32($b,0)
  $vCnt = [BitConverter]::ToUInt32($b,16)
  $tCnt = [BitConverter]::ToUInt32($b,20)
  $aabb = @(); for ($i=0; $i -lt 6; $i++) { $aabb += [BitConverter]::ToSingle($b,80+$i*4) }

  # measure the real extent from the vertex array rather than trusting the AABB
  $ok = ($vOff -ge 0) -and ($vOff + $vCnt*12 -le $b.Length) -and ($vCnt -gt 0)
  if ($ok) {
    $mn = @([float]::MaxValue)*3; $mx = @([float]::MinValue)*3
    $mn = New-Object 'float[]' 3; $mx = New-Object 'float[]' 3
    for ($c=0; $c -lt 3; $c++) { $mn[$c]=[float]::MaxValue; $mx[$c]=[float]::MinValue }
    for ($v=0; $v -lt $vCnt; $v++) {
      for ($c=0; $c -lt 3; $c++) {
        $f = [BitConverter]::ToSingle($b,$vOff+$v*12+$c*4)
        if ($f -lt $mn[$c]) { $mn[$c]=$f }; if ($f -gt $mx[$c]) { $mx[$c]=$f }
      }
    }
  }
  Write-Output ("{0,-34} {1,8} B  verts={2,-6} tris={3,-6} vOff={4}" -f $name,$b.Length,$vCnt,$tCnt,$vOff)
  Write-Output ("     AABB   {0}" -f (($aabb | ForEach-Object { '{0,9:N2}' -f $_ }) -join ' '))
  if ($ok) {
    Write-Output ("     verts  {0}" -f ((@($mn[0],$mn[1],$mn[2],$mx[0],$mx[1],$mx[2]) | ForEach-Object { '{0,9:N2}' -f $_ }) -join ' '))
    Write-Output ("     size   {0,9:N2} x {1,9:N2} x {2,9:N2}" -f ($mx[0]-$mn[0]),($mx[1]-$mn[1]),($mx[2]-$mn[2]))
  }
}
