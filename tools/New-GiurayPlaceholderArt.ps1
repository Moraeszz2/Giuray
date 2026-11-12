# Draws Giuray's placeholder artwork (a rounded square with a "G") into dist/branding, until a
# real logo exists: the icon (1024 px) and a wide logo with the name. Run Generate-GiurayIcons.ps1
# afterwards to build giuray.ico, giuray.icns and giuray.png from the icon.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$branding = [IO.Path]::GetFullPath("$PSScriptRoot/../dist/branding")

function New-RoundedPath([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $path = New-Object Drawing.Drawing2D.GraphicsPath
    $path.AddArc($x, $y, 2 * $r, 2 * $r, 180, 90)
    $path.AddArc($x + $w - 2 * $r, $y, 2 * $r, 2 * $r, 270, 90)
    $path.AddArc($x + $w - 2 * $r, $y + $h - 2 * $r, 2 * $r, 2 * $r, 0, 90)
    $path.AddArc($x, $y + $h - 2 * $r, 2 * $r, 2 * $r, 90, 90)
    $path.CloseFigure()
    return $path
}

function Draw-Icon([Drawing.Graphics]$g, [float]$x, [float]$y, [float]$size) {
    $rect = New-Object Drawing.RectangleF($x, $y, $size, $size)
    $brush = New-Object Drawing.Drawing2D.LinearGradientBrush($rect,
        [Drawing.Color]::FromArgb(255, 124, 58, 237), [Drawing.Color]::FromArgb(255, 14, 165, 233), 45.0)
    $path = New-RoundedPath $x $y $size $size ($size * 0.22)
    $g.FillPath($brush, $path)
    $font = New-Object Drawing.Font('Segoe UI', ($size * 0.55), [Drawing.FontStyle]::Bold, [Drawing.GraphicsUnit]::Pixel)
    $format = New-Object Drawing.StringFormat
    $format.Alignment = [Drawing.StringAlignment]::Center
    $format.LineAlignment = [Drawing.StringAlignment]::Center
    $g.DrawString('G', $font, [Drawing.Brushes]::White, $rect, $format)
    $font.Dispose(); $brush.Dispose(); $path.Dispose()
}

function New-Canvas([int]$w, [int]$h) {
    $bitmap = New-Object Drawing.Bitmap($w, $h, ([Drawing.Imaging.PixelFormat]::Format32bppArgb))
    $g = [Drawing.Graphics]::FromImage($bitmap)
    $g.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.TextRenderingHint = [Drawing.Text.TextRenderingHint]::AntiAliasGridFit
    $g.Clear([Drawing.Color]::Transparent)
    return $bitmap, $g
}

$bitmap, $g = New-Canvas 1024 1024
Draw-Icon $g 32 32 960
$bitmap.Save("$branding/giuray_icon.png", [Drawing.Imaging.ImageFormat]::Png)
$g.Dispose(); $bitmap.Dispose()

foreach ($name in 'giuray_icon_with_logo', 'giuray_logo') {
    $bitmap, $g = New-Canvas 1800 600
    Draw-Icon $g 40 60 480
    $font = New-Object Drawing.Font('Segoe UI', 300, [Drawing.FontStyle]::Bold, [Drawing.GraphicsUnit]::Pixel)
    $text = New-Object Drawing.SolidBrush([Drawing.Color]::FromArgb(255, 124, 58, 237))
    $g.DrawString('Giuray', $font, $text, 560, 110)
    $bitmap.Save("$branding/$name.png", [Drawing.Imaging.ImageFormat]::Png)
    $font.Dispose(); $text.Dispose(); $g.Dispose(); $bitmap.Dispose()
}

foreach ($name in 'giuray_square_grid', 'giuray_vertical_grid') {
    $w = 920; $h = if ($name -eq 'giuray_square_grid') { 920 } else { 1380 }
    $bitmap, $g = New-Canvas $w $h
    $g.Clear([Drawing.Color]::FromArgb(255, 17, 24, 39))
    $size = [Math]::Min($w, $h) * 0.6
    Draw-Icon $g (($w - $size) / 2) (($h - $size) / 2) $size
    $bitmap.Save("$branding/$name.png", [Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bitmap.Dispose()
}
Write-Host "Placeholder art written to $branding"
