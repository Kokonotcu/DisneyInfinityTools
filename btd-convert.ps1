# Builds a 3.0-format .btd carrying a 1.0 world's collision mesh.
#
# The region before the vertices is two sections, described by the header in
# both games, and it is NOT optional: carrying the donor's 480-triangle tree in
# front of 7,056 triangles produced no collision at all.
#
#   sectionA  starts at 64, holds the tree. Its size does not scale cleanly
#             with triangle count (6.2 - 7.1 B/tri over 267 shipped files),
#             because it depends on how the geometry is distributed. It cannot
#             be computed, only built -- and the AABB at +80 lives INSIDE it,
#             as the root node's bounds.
#   sectionB  4 x triangles + 72 in 3.0, exact over all 267; 1.0 uses + 56.
#
# So do not synthesise either. The 1.0 file already carries a tree built for
# its own mesh -- Sumo's is 48,704 B for 7,056 triangles, 6.90 B/tri, squarely
# inside 3.0's range and within 64 bytes of proto_artset_tatooine's tree for a
# near-identical triangle count. Carry BOTH sections over from the source and
# relay them out in 3.0 header order.
#
# Layout produced, matching a stock 3.0 file:
#   +0    u64 vertexOffset          +8   u64 indexOffset
#   +16   u32 vertexCount           +20  u32 triangleCount
#   +40   end of index array        +48  per-triangle array offset, +52 count
#   +80   AABB: min xyz + pad, max xyz + pad   (1.0 margin, taken from source)
#   then  donor's undecoded region verbatim, vertices f32x3, indices u16 x3,
#         an 8-byte gap, and a per-triangle u32 array of zeros

param(
  [string]$Source = 'D:\DI-Extract\1.0\assets\worlds\adv_cbt_sumo\adv_cbt_sumo.btd',
  [string]$Donor  = 'D:\DI-Extract\3.0\assets\worlds\out_intro2\out_intro2.btd',
  # NOTE: not named $Out. PowerShell variables are case-insensitive, so a
  # parameter $Out and a working buffer $out are the same variable.
  [Parameter(Mandatory=$true)][string]$OutPath,
  [float[]]$Offset = @(0,0,0),      # must match the -Offset given to mesh-into.ps1
  [switch]$ZeroTree                 # blank the carried region instead of copying it
)

$ErrorActionPreference = 'Stop'

$src = [IO.File]::ReadAllBytes($Source)
$dnr = [IO.File]::ReadAllBytes($Donor)

# --- 1.0 source: vertexOffset +16, vertexCount +20, indexOffset +24, triCount +28
$sVOff = [int][BitConverter]::ToUInt32($src,16)
$sVCnt = [int][BitConverter]::ToUInt32($src,20)
$sIOff = [int][BitConverter]::ToUInt32($src,24)
$sTCnt = [int][BitConverter]::ToUInt32($src,28)
# 1.0 puts the two section descriptors at +0..+12, where 3.0 puts u64 offsets
$sAOff = [int][BitConverter]::ToUInt32($src,0)
$sALen = [int][BitConverter]::ToUInt32($src,4)
$sBOff = [int][BitConverter]::ToUInt32($src,8)
$sBLen = [int][BitConverter]::ToUInt32($src,12)
if ($sVOff + $sVCnt*12 -ne $sIOff) { throw "source vertex array does not close: $sVOff + $sVCnt*12 != $sIOff" }
if ($sIOff + $sTCnt*6 -gt $src.Length) { throw "source index array runs past the file" }
if ($sAOff + $sALen -ne $sBOff) { throw "source section A does not close: $sAOff + $sALen != $sBOff" }
if ($sBOff + $sBLen -ne $sVOff) { throw "source section B does not close: $sBOff + $sBLen != $sVOff" }

# --- 3.0 layout: same section order, header fields at different offsets
# Section B's header is the same in both games except that its four leading
# pointer fields are u32 in 1.0 and u64 in 3.0 -- exactly the 16 bytes by which
# 4T+56 differs from 4T+72. Everything from the header-size field onward is
# identical, so widening is: 32 zero bytes, then the source's B from +16 on,
# then set the size field to 72. Section A needs no conversion; its header is
# byte-for-byte the same shape in both, 16 zeros then the AABB min and max.
$aConstPath = Join-Path $PSScriptRoot 'btd-a-const.bin'
if (-not (Test-Path $aConstPath)) { throw "missing $aConstPath (the 120-byte sectionA constant)" }
$aConst = [IO.File]::ReadAllBytes($aConstPath)
if ($aConst.Length -ne 120) { throw "btd-a-const.bin must be 120 bytes, is $($aConst.Length)" }

$bGrow = 16
$aOff = 64
$aNewLen = $sALen + 16
$bOff = $aOff + $aNewLen
$bNewLen = $sBLen + $bGrow
$vOff = $bOff + $bNewLen
$iOff = $vOff + $sVCnt*12
$iEnd = $iOff + $sTCnt*6
$pOff = $iEnd + 8
$total = $pOff + $sTCnt*4

$buf = [byte[]]::new($total)
[Array]::Copy($dnr, 0, $buf, 0, 64)                       # 3.0 header shell

# --- sectionA: 1.0 -> 3.0, derived from 322 matched pairs and verified by
# reconstructing 323 of 330 shipped 3.0 files byte-exactly from their 1.0 twins.
# Only a descriptor at the head of the tree changes; the tree body is identical.
#   A[0..63]     carried  (16 zeros, then the AABB min/max as the root bounds)
#   A[64..183]   a 120-byte constant, identical in all 322 pairs
#   A[184..199]  1.0's A[64..79]   -- tag 0x114, node count, flags
#   A[200..203]  1.0's A[180..183] -- a u32 that scales with node count
#   A[204..]     1.0's A[188..]    -- the tree body, unchanged
# The 100-odd bytes of 1.0's A[80..179] are the old-format descriptor entries,
# which the constant block replaces.
[Array]::Copy($src, $sAOff,       $buf, $aOff,       64)
[Array]::Copy($aConst, 0,         $buf, $aOff + 64,  120)
[Array]::Copy($src, $sAOff + 64,  $buf, $aOff + 184, 16)
[Array]::Copy($src, $sAOff + 180, $buf, $aOff + 200, 4)
[Array]::Copy($src, $sAOff + 188, $buf, $aOff + 204, $sALen - 188)
# 32 zero bytes of widened pointers, then the source's B from +16 onward
[Array]::Copy($src, $sBOff + 16, $buf, $bOff + 32, $sBLen - 16)
[BitConverter]::GetBytes([uint64]72).CopyTo($buf, $bOff + 16)   # header size field
if ($ZeroTree) { for ($i = 112; $i -lt $vOff; $i++) { $buf[$i] = 0 } }

# vertices, with the same offset mesh-into.ps1 baked into the render mesh
for ($v = 0; $v -lt $sVCnt; $v++) {
  for ($c = 0; $c -lt 3; $c++) {
    $f = [BitConverter]::ToSingle($src, $sVOff + $v*12 + $c*4) + $Offset[$c]
    [BitConverter]::GetBytes([float]$f).CopyTo($buf, $vOff + $v*12 + $c*4)
  }
}
[Array]::Copy($src, $sIOff, $buf, $iOff, $sTCnt*6)   # indices verbatim, u16 triples

# AABB from the source, shifted; both games store min xyz + pad, max xyz + pad
foreach ($base in 80,96) {
  for ($c = 0; $c -lt 3; $c++) {
    $f = [BitConverter]::ToSingle($src, $base + $c*4) + $Offset[$c]
    [BitConverter]::GetBytes([float]$f).CopyTo($buf, $base + $c*4)
  }
}

[BitConverter]::GetBytes([uint64]$vOff).CopyTo($buf,0)
[BitConverter]::GetBytes([uint64]$iOff).CopyTo($buf,8)
[BitConverter]::GetBytes([uint32]$sVCnt).CopyTo($buf,16)
[BitConverter]::GetBytes([uint32]$sTCnt).CopyTo($buf,20)
[BitConverter]::GetBytes([uint32]$aOff).CopyTo($buf,24)
[BitConverter]::GetBytes([uint32]$aNewLen).CopyTo($buf,28)
[BitConverter]::GetBytes([uint32]$bOff).CopyTo($buf,32)
[BitConverter]::GetBytes([uint32]$bNewLen).CopyTo($buf,36)
[BitConverter]::GetBytes([uint32]$iEnd).CopyTo($buf,40)
[BitConverter]::GetBytes([uint32]1).CopyTo($buf,44)
[BitConverter]::GetBytes([uint32]$pOff).CopyTo($buf,48)
[BitConverter]::GetBytes([uint32]$sTCnt).CopyTo($buf,52)

[IO.File]::WriteAllBytes($OutPath,$buf)
"$([IO.Path]::GetFileName($Source)) -> $OutPath"
"  $sVCnt verts, $sTCnt tris, offset $($Offset -join ',')"
"  vertices +$vOff  indices +$iOff  perTri +$pOff  total $total B"
"  sectionA +$aOff len $aNewLen (widened from $sALen)   sectionB +$bOff len $bNewLen (= 4*T + $($bNewLen - 4*$sTCnt), widened from $sBLen)"
"  sections carried from $([IO.Path]::GetFileName($Source))$(if($ZeroTree){' then ZEROED'}else{''})"
