param([Parameter(ValueFromRemainingArguments = $true)][string[]]$files)
Add-Type -AssemblyName System.Drawing
foreach ($f in $files) {
    $p = (Resolve-Path $f).Path
    $i = [System.Drawing.Image]::FromFile($p)
    $i.Save([IO.Path]::ChangeExtension($p, '.png'), [System.Drawing.Imaging.ImageFormat]::Png)
    $i.Dispose()
}
