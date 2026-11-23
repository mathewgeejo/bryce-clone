param([string]$out, [int]$cols = 2, [Parameter(ValueFromRemainingArguments = $true)][string[]]$files)
Add-Type -AssemblyName System.Drawing
$imgs = $files | ForEach-Object { [System.Drawing.Image]::FromFile((Resolve-Path $_)) }
$w = $imgs[0].Width; $h = $imgs[0].Height
$rows = [math]::Ceiling($imgs.Count / $cols)
$bmp = New-Object System.Drawing.Bitmap ($w * $cols), ($h * $rows)
$g = [System.Drawing.Graphics]::FromImage($bmp)
for ($i = 0; $i -lt $imgs.Count; $i++) { $g.DrawImage($imgs[$i], ($i % $cols) * $w, [math]::Floor($i / $cols) * $h, $w, $h) }
$bmp.Save($out, [System.Drawing.Imaging.ImageFormat]::Png)
