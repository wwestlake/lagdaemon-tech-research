# Generate branded assets for Djehuti Electronics Workbench installer
# - app.ico (multi-resolution Windows icon)
# - wizard_image.bmp (welcome/finish banner artwork for Inno Setup)
# - wizard_small.bmp (header icon for Inno Setup)

Add-Type -AssemblyName System.Drawing

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$installerDir = Split-Path -Parent $scriptDir
$assetsDir = Join-Path $installerDir "assets"
if (-not (Test-Path $assetsDir)) {
    New-Item -ItemType Directory -Path $assetsDir -Force | Out-Null
}

$logoPath = "D:\000 Tech Research\Electronics Design Tool\assets\DjehutiElectronicsLabLogo.png"
if (-not (Test-Path $logoPath)) {
    Write-Error "Logo not found at $logoPath"
    exit 1
}

$srcImage = [System.Drawing.Image]::FromFile($logoPath)

# 1. Generate multi-resolution standard DIB .ico for Inno Setup compatibility
function Create-MultiResIco {
    param(
        [System.Drawing.Image]$Source,
        [string]$OutputPath,
        [int[]]$Sizes = @(16, 24, 32, 48, 64, 128, 256)
    )

    $entries = @()
    foreach ($sz in $Sizes) {
        $bmp = New-Object System.Drawing.Bitmap $sz, $sz
        $g = [System.Drawing.Graphics]::FromImage($bmp)
        $g.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
        $g.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality
        $g.Clear([System.Drawing.Color]::Transparent)
        $g.DrawImage($Source, 0, 0, $sz, $sz)
        $g.Dispose()

        $ms = New-Object System.IO.MemoryStream
        $bw = New-Object System.IO.BinaryWriter $ms

        # BITMAPINFOHEADER (40 bytes)
        $bw.Write([UInt32]40)              # biSize
        $bw.Write([Int32]$sz)              # biWidth
        $bw.Write([Int32]($sz * 2))        # biHeight (doubled for XOR + AND masks)
        $bw.Write([UInt16]1)               # biPlanes
        $bw.Write([UInt16]32)              # biBitCount
        $bw.Write([UInt32]0)               # biCompression (BI_RGB)
        $bw.Write([UInt32]($sz * $sz * 4)) # biSizeImage
        $bw.Write([Int32]0)                # biXPelsPerMeter
        $bw.Write([Int32]0)                # biYPelsPerMeter
        $bw.Write([UInt32]0)               # biClrUsed
        $bw.Write([UInt32]0)               # biClrImportant

        $rect = New-Object System.Drawing.Rectangle 0, 0, $sz, $sz
        $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
        $stride = [Math]::Abs($data.Stride)
        $pixelBytes = New-Object byte[] ($stride * $sz)
        [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $pixelBytes, 0, $pixelBytes.Length)
        $bmp.UnlockBits($data)
        $bmp.Dispose()

        # Write rows bottom-up
        for ($y = $sz - 1; $y -ge 0; $y--) {
            $rowOffset = $y * $stride
            $bw.Write($pixelBytes, $rowOffset, ($sz * 4))
        }

        # 1-bit AND mask (all zeros for 32bpp with alpha), DWORD-aligned rows
        $andRowBytes = [int]([Math]::Ceiling($sz / 32.0) * 4)
        $andMask = New-Object byte[] ($andRowBytes * $sz)
        $bw.Write($andMask)

        $bw.Flush()
        $entries += ,@($sz, $ms.ToArray())
        $bw.Dispose()
        $ms.Dispose()
    }

    $fs = [System.IO.File]::Create($OutputPath)
    $bw = New-Object System.IO.BinaryWriter $fs
    $bw.Write([UInt16]0) # Reserved
    $bw.Write([UInt16]1) # Icon type
    $bw.Write([UInt16]$entries.Count) # Count

    $offset = 6 + 16 * $entries.Count
    foreach ($item in $entries) {
        $sz = $item[0]
        $bytes = $item[1]
        [byte]$bWidth = if ($sz -ge 256) { 0 } else { $sz }
        [byte]$bHeight = if ($sz -ge 256) { 0 } else { $sz }
        $bw.Write($bWidth)
        $bw.Write($bHeight)
        $bw.Write([byte]0) # Color count
        $bw.Write([byte]0) # Reserved
        $bw.Write([UInt16]1) # Color planes
        $bw.Write([UInt16]32) # Bits per pixel
        $bw.Write([UInt32]$bytes.Length)
        $bw.Write([UInt32]$offset)
        $offset += $bytes.Length
    }
    foreach ($item in $entries) {
        $bw.Write($item[1])
    }
    $bw.Flush()
    $fs.Dispose()
}

$icoPath = Join-Path $assetsDir "app.ico"
Create-MultiResIco -Source $srcImage -OutputPath $icoPath
Write-Host "Generated $icoPath"

# 2. Generate modern wizard image (164 x 314 standard Inno Setup left banner)
# Deep slate/dark background gradient matching DEWB branding (#0B1015 to #070B0E)
# with the centered badge in the upper portion and subtle product accent.
$wWidth = 164
$wHeight = 314
$wizBmp = New-Object System.Drawing.Bitmap $wWidth, $wHeight
$wg = [System.Drawing.Graphics]::FromImage($wizBmp)
$wg.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$wg.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality

$rect = New-Object System.Drawing.Rectangle 0, 0, $wWidth, $wHeight
$cTop = [System.Drawing.Color]::FromArgb(255, 12, 19, 24)
$cBottom = [System.Drawing.Color]::FromArgb(255, 5, 8, 11)
$brush = [System.Drawing.Drawing2D.LinearGradientBrush]::new($rect, $cTop, $cBottom, [System.Drawing.Drawing2D.LinearGradientMode]::Vertical)
$wg.FillRectangle($brush, $rect)
$brush.Dispose()

# Draw subtle accent lines
$pen = [System.Drawing.Pen]::new([System.Drawing.Color]::FromArgb(40, 0, 180, 200), [single]1)
$wg.DrawLine($pen, 16, 220, 148, 220)
$wg.DrawLine($pen, 32, 224, 132, 224)
$pen.Dispose()

# Draw logo badge scaled to 128x128 in upper section
$logoX = ($wWidth - 128) / 2
$logoY = 48
$wg.DrawImage($srcImage, $logoX, $logoY, 128, 128)

# Subtle text: DEWB
$font = [System.Drawing.Font]::new("Segoe UI", [single]9, [System.Drawing.FontStyle]::Bold)
$textBrush = [System.Drawing.SolidBrush]::new([System.Drawing.Color]::FromArgb(180, 200, 220))
$sf = [System.Drawing.StringFormat]::new()
$sf.Alignment = [System.Drawing.StringAlignment]::Center
$wg.DrawString("Djehuti Electronics", $font, $textBrush, [single]($wWidth / 2), [single]184, $sf)
$wg.DrawString("Workbench", $font, $textBrush, [single]($wWidth / 2), [single]200, $sf)
$font.Dispose()
$textBrush.Dispose()
$sf.Dispose()
$wg.Dispose()

$wizBmpPath = Join-Path $assetsDir "wizard_image.bmp"
$wizBmp.Save($wizBmpPath, [System.Drawing.Imaging.ImageFormat]::Bmp)
$wizBmp.Dispose()
Write-Host "Generated $wizBmpPath"

# 3. Generate small wizard image (55 x 55 top-right banner logo)
$sSize = 55
$smallBmp = New-Object System.Drawing.Bitmap $sSize, $sSize
$sg = [System.Drawing.Graphics]::FromImage($smallBmp)
$sg.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
$sg.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
$sRect = New-Object System.Drawing.Rectangle 0, 0, $sSize, $sSize
$sBrush = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 12, 19, 24))
$sg.FillRectangle($sBrush, $sRect)
$sBrush.Dispose()

# Draw scaled logo
$sg.DrawImage($srcImage, 3, 3, 49, 49)
$sg.Dispose()

$wizSmallPath = Join-Path $assetsDir "wizard_small.bmp"
$smallBmp.Save($wizSmallPath, [System.Drawing.Imaging.ImageFormat]::Bmp)
$smallBmp.Dispose()
Write-Host "Generated $wizSmallPath"

$srcImage.Dispose()
Write-Host "All installer branding assets successfully generated."
