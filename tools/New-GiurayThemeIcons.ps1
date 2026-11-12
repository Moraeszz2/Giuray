# Draws the Giuray theme's icons (dist/qt_themes/giuray): thin line icons in the theme's muted
# gray, with the accent purple only where an icon is an action. Each is drawn large and scaled
# down for smooth edges; QSS images get an @2x copy for high-DPI screens.
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$theme = [IO.Path]::GetFullPath("$PSScriptRoot/../dist/qt_themes/giuray")

$Muted = [Drawing.Color]::FromArgb(255, 161, 161, 170)   # zinc-400
$Strong = [Drawing.Color]::FromArgb(255, 228, 228, 231)  # zinc-200
$Dim = [Drawing.Color]::FromArgb(255, 113, 113, 122)     # zinc-500
$Accent = [Drawing.Color]::FromArgb(255, 239, 68, 68)    # red-500 (reads better small)
$Danger = [Drawing.Color]::FromArgb(255, 245, 158, 11)
$White = [Drawing.Color]::White

# Every drawing works on a 24-unit grid, like common line icon sets.
function New-Icon([string]$path, [int]$size, [scriptblock]$draw) {
    $scale = 8
    $big = New-Object Drawing.Bitmap(($size * $scale), ($size * $scale), ([Drawing.Imaging.PixelFormat]::Format32bppArgb))
    $g = [Drawing.Graphics]::FromImage($big)
    $g.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([Drawing.Color]::Transparent)
    $unit = $size * $scale / 24.0
    $g.ScaleTransform($unit, $unit)
    & $draw $g
    $g.Dispose()
    $out = New-Object Drawing.Bitmap($size, $size, ([Drawing.Imaging.PixelFormat]::Format32bppArgb))
    $og = [Drawing.Graphics]::FromImage($out)
    $og.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $og.PixelOffsetMode = [Drawing.Drawing2D.PixelOffsetMode]::HighQuality
    $og.DrawImage($big, 0, 0, $size, $size)
    $og.Dispose(); $big.Dispose()
    New-Item -ItemType Directory -Force (Split-Path $path) | Out-Null
    $out.Save($path, [Drawing.Imaging.ImageFormat]::Png)
    $out.Dispose()
}

function Pen([Drawing.Color]$color, [float]$width = 1.5) {
    $pen = New-Object Drawing.Pen($color, $width)
    $pen.StartCap = [Drawing.Drawing2D.LineCap]::Round
    $pen.EndCap = [Drawing.Drawing2D.LineCap]::Round
    $pen.LineJoin = [Drawing.Drawing2D.LineJoin]::Round
    return $pen
}

function Rounded([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $p = New-Object Drawing.Drawing2D.GraphicsPath
    $p.AddArc($x, $y, 2 * $r, 2 * $r, 180, 90)
    $p.AddArc($x + $w - 2 * $r, $y, 2 * $r, 2 * $r, 270, 90)
    $p.AddArc($x + $w - 2 * $r, $y + $h - 2 * $r, 2 * $r, 2 * $r, 0, 90)
    $p.AddArc($x, $y + $h - 2 * $r, 2 * $r, 2 * $r, 90, 90)
    $p.CloseFigure()
    return $p
}

function Folder([Drawing.Graphics]$g, [Drawing.Color]$color) {
    $p = New-Object Drawing.Drawing2D.GraphicsPath
    $p.AddLines([Drawing.PointF[]]@(
        [Drawing.PointF]::new(3, 7), [Drawing.PointF]::new(3, 5.5), [Drawing.PointF]::new(4.5, 4),
        [Drawing.PointF]::new(9, 4), [Drawing.PointF]::new(11, 6.5), [Drawing.PointF]::new(19.5, 6.5),
        [Drawing.PointF]::new(21, 8), [Drawing.PointF]::new(21, 18.5), [Drawing.PointF]::new(19.5, 20),
        [Drawing.PointF]::new(4.5, 20), [Drawing.PointF]::new(3, 18.5)))
    $p.CloseFigure()
    $g.DrawPath((Pen $color), $p)
    $g.DrawLine((Pen $color), 3, 9.5, 21, 9.5)
}

function Plus([Drawing.Graphics]$g, [Drawing.Color]$color, [float]$cx, [float]$cy, [float]$r, [float]$w = 1.5) {
    $g.DrawLine((Pen $color $w), $cx - $r, $cy, $cx + $r, $cy)
    $g.DrawLine((Pen $color $w), $cx, $cy - $r, $cx, $cy + $r)
}

function Chevron([Drawing.Graphics]$g, [Drawing.Color]$color, [string]$dir, [float]$w = 2) {
    $pts = switch ($dir) {
        'down'  { @([Drawing.PointF]::new(7, 10), [Drawing.PointF]::new(12, 15), [Drawing.PointF]::new(17, 10)) }
        'up'    { @([Drawing.PointF]::new(7, 14), [Drawing.PointF]::new(12, 9), [Drawing.PointF]::new(17, 14)) }
        'right' { @([Drawing.PointF]::new(10, 7), [Drawing.PointF]::new(15, 12), [Drawing.PointF]::new(10, 17)) }
        'left'  { @([Drawing.PointF]::new(14, 7), [Drawing.PointF]::new(9, 12), [Drawing.PointF]::new(14, 17)) }
    }
    $g.DrawLines((Pen $color $w), [Drawing.PointF[]]$pts)
}

$icons = "$theme/icons"
foreach ($size in 48) {
    $dir = "$icons/${size}x${size}"
    New-Icon "$dir/folder.png" $size { param($g) Folder $g $Muted }
    New-Icon "$dir/bad_folder.png" $size {
        param($g)
        Folder $g $Dim
        $g.FillEllipse((New-Object Drawing.SolidBrush($Danger)), 16, 14, 7, 7)
    }
    New-Icon "$dir/list-add.png" $size {
        param($g)
        $g.DrawPath((Pen $Accent), (Rounded 3 3 18 18 5))
        Plus $g $Accent 12 12 4.5
    }
    New-Icon "$dir/star.png" $size {
        param($g)
        $pts = for ($i = 0; $i -lt 10; $i++) {
            $r = if ($i % 2 -eq 0) { 9 } else { 4 }
            $a = [Math]::PI * (-90 + 36 * $i) / 180
            [Drawing.PointF]::new(12 + $r * [Math]::Cos($a), 12.5 + $r * [Math]::Sin($a))
        }
        $g.DrawPolygon((Pen $Strong), [Drawing.PointF[]]$pts)
    }
    New-Icon "$dir/chip.png" $size {
        param($g)
        $g.DrawPath((Pen $Muted), (Rounded 6 6 12 12 2))
        $g.DrawRectangle((Pen $Muted), 9.5, 9.5, 5, 5)
        foreach ($t in 9, 12, 15) {
            $g.DrawLine((Pen $Muted), $t, 3, $t, 6); $g.DrawLine((Pen $Muted), $t, 18, $t, 21)
            $g.DrawLine((Pen $Muted), 3, $t, 6, $t); $g.DrawLine((Pen $Muted), 18, $t, 21, $t)
        }
    }
    New-Icon "$dir/sd_card.png" $size {
        param($g)
        $p = New-Object Drawing.Drawing2D.GraphicsPath
        $p.AddLines([Drawing.PointF[]]@([Drawing.PointF]::new(9, 3), [Drawing.PointF]::new(17, 3),
            [Drawing.PointF]::new(19, 5), [Drawing.PointF]::new(19, 19.5), [Drawing.PointF]::new(17.5, 21),
            [Drawing.PointF]::new(6.5, 21), [Drawing.PointF]::new(5, 19.5), [Drawing.PointF]::new(5, 7)))
        $p.CloseFigure()
        $g.DrawPath((Pen $Muted), $p)
        foreach ($x in 10, 13, 16) { $g.DrawLine((Pen $Muted), $x, 6, $x, 9) }
    }
}

$dir16 = "$icons/16x16"
New-Icon "$dir16/view-refresh.png" 16 {
    param($g)
    $g.DrawArc((Pen $Muted 2), 5, 5, 14, 14, -60, 300)
    $g.DrawLines((Pen $Muted 2), [Drawing.PointF[]]@([Drawing.PointF]::new(19, 3.5), [Drawing.PointF]::new(19, 8.5), [Drawing.PointF]::new(14, 8.5)))
}
New-Icon "$dir16/search.png" 16 {
    param($g)
    $g.DrawEllipse((Pen $Muted 2), 4, 4, 12, 12)
    $g.DrawLine((Pen $Muted 2), 14.5, 14.5, 20, 20)
}
New-Icon "$dir16/view-list.png" 16 {
    param($g)
    foreach ($y in 6, 12, 18) { $g.DrawLine((Pen $Muted 2), 4, $y, 20, $y) }
}
New-Icon "$dir16/view-grid.png" 16 {
    param($g)
    foreach ($p in @(@(4, 4), @(13, 4), @(4, 13), @(13, 13))) { $g.DrawPath((Pen $Muted 2), (Rounded $p[0] $p[1] 7 7 1.5)) }
}
New-Icon "$dir16/list-add.png" 16 { param($g) Plus $g $White 12 12 7 2 }

$icons256 = "$icons/256x256"
New-Icon "$icons256/plus_folder.png" 256 {
    param($g)
    Folder $g $Dim
    $g.FillEllipse((New-Object Drawing.SolidBrush([Drawing.Color]::FromArgb(255, 14, 14, 16))), 13, 12, 10, 10)
    Plus $g $Accent 18 17 3
}

# Images the stylesheet draws: check marks and arrows, at 1x and @2x.
$rc = "$theme/rc"
foreach ($scale in 1, 2) {
    $suffix = if ($scale -eq 2) { '@2x' } else { '' }
    New-Icon "$rc/check$suffix.png" (12 * $scale) {
        param($g)
        $g.DrawLines((Pen $White 3), [Drawing.PointF[]]@([Drawing.PointF]::new(5, 12.5), [Drawing.PointF]::new(10, 17.5), [Drawing.PointF]::new(19, 7)))
    }
    New-Icon "$rc/indeterminate$suffix.png" (12 * $scale) { param($g) $g.DrawLine((Pen $White 3), 6, 12, 18, 12) }
    foreach ($d in 'down', 'up', 'right', 'left') {
        New-Icon "$rc/arrow-$d$suffix.png" (12 * $scale) { param($g) Chevron $g $Muted $d 2.5 }
    }
    New-Icon "$rc/arrow-down-disabled$suffix.png" (12 * $scale) { param($g) Chevron $g $Dim 'down' 2.5 }
}

Set-Content -Path "$icons/index.theme" -Encoding ascii -Value @"
[Icon Theme]
Name=giuray
Comment=Giuray (line icons)
Inherits=colorful_dark
Directories=16x16,48x48,256x256

[16x16]
Size=16

[48x48]
Size=48

[256x256]
Size=256
"@
Write-Host "Giuray theme icons written to $theme"
