[CmdletBinding()]
param(
    [string]$OutputPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$repository = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
if (-not $OutputPath) {
    $OutputPath = Join-Path $repository 'assets\branding\PrismForge.ico'
}
$output = [System.IO.Path]::GetFullPath($OutputPath)
$directory = Split-Path -Parent $output
New-Item -ItemType Directory -Path $directory -Force | Out-Null

Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class NativeIcon {
  [DllImport("user32.dll", CharSet = CharSet.Auto)]
  public static extern bool DestroyIcon(IntPtr handle);
}
"@

function New-PrismBitmap {
    param([int]$Size)
    $bitmap = New-Object System.Drawing.Bitmap $Size, $Size
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.Clear([System.Drawing.Color]::FromArgb(255, 7, 9, 16))

    $background = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        [System.Drawing.Point]::new(0, 0),
        [System.Drawing.Point]::new($Size, $Size),
        [System.Drawing.Color]::FromArgb(255, 12, 18, 34),
        [System.Drawing.Color]::FromArgb(255, 7, 9, 16))
    $graphics.FillEllipse($background, 1, 1, ($Size - 2), ($Size - 2))
    $background.Dispose()

    $penWidth = [Math]::Max(1.0, $Size / 18.0)
    $centerX = $Size / 2.0
    $topY = $Size * 0.18
    $leftX = $Size * 0.22
    $rightX = $Size * 0.78
    $bottomY = $Size * 0.82

    $points = @(
        [System.Drawing.PointF]::new($centerX, $topY),
        [System.Drawing.PointF]::new($rightX, $bottomY),
        [System.Drawing.PointF]::new($leftX, $bottomY)
    )

    $faceBrush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        [System.Drawing.Point]::new([int]$leftX, [int]$topY),
        [System.Drawing.Point]::new([int]$rightX, [int]$bottomY),
        [System.Drawing.Color]::FromArgb(210, 69, 233, 255),
        [System.Drawing.Color]::FromArgb(210, 255, 80, 203))
    $graphics.FillPolygon($faceBrush, $points)
    $faceBrush.Dispose()

    $edgePen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(255, 238, 242, 255)), $penWidth
    $graphics.DrawPolygon($edgePen, $points)
    $edgePen.Dispose()

    $innerPen = New-Object System.Drawing.Pen ([System.Drawing.Color]::FromArgb(180, 150, 115, 255)), ($penWidth * 0.65)
    $graphics.DrawLine($innerPen, $centerX, $topY, $centerX, $bottomY)
    $graphics.DrawLine($innerPen, $leftX, $bottomY, $centerX, $topY)
    $innerPen.Dispose()

    $glowBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(90, 69, 233, 255))
    $glowSize = [Math]::Max(2.0, $Size / 10.0)
    $graphics.FillEllipse($glowBrush, ($centerX - ($glowSize / 2)), ($topY - ($glowSize / 2)), $glowSize, $glowSize)
    $glowBrush.Dispose()

    $graphics.Dispose()
    return $bitmap
}

function Save-Icon {
    param(
        [System.Drawing.Bitmap]$Image,
        [string]$Path
    )
    $iconHandle = $Image.GetHicon()
    try {
        $icon = [System.Drawing.Icon]::FromHandle($iconHandle)
        $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Create)
        try {
            $icon.Save($stream)
        }
        finally {
            $stream.Dispose()
        }
    }
    finally {
        [NativeIcon]::DestroyIcon($iconHandle) | Out-Null
    }
}

$bitmap = New-PrismBitmap -Size 256
try {
    Save-Icon -Image $bitmap -Path $output
}
finally {
    $bitmap.Dispose()
}

Write-Output "Wrote $output"
