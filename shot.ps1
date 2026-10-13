# Screenshot of the whole virtual screen (the running game), for checks between harness steps.
#   powershell -File shot.ps1 [-Name x] [-Out dir]   -> prints the PNG path
param([string]$Name = "shot-$(Get-Date -Format HHmmss)", [string]$Out = "$env:TEMP\di3harness")
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
New-Item -ItemType Directory -Force $Out | Out-Null
$b = [System.Windows.Forms.SystemInformation]::VirtualScreen
$bmp = New-Object System.Drawing.Bitmap $b.Width, $b.Height
[System.Drawing.Graphics]::FromImage($bmp).CopyFromScreen($b.Left, $b.Top, 0, 0, $bmp.Size)
$f = Join-Path $Out "$Name.png"; $bmp.Save($f, [System.Drawing.Imaging.ImageFormat]::Png); $f
