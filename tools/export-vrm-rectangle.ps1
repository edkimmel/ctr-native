[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string[]]$VrmPath,

    # SourceX/SourceY/Width/Height are PSX VRAM *words*, as recorded by the
    # presentation trace and manifest.  The output width is expanded to texels.
    [Parameter(Mandatory = $true)]
    [ValidateSet(4, 8, 16)]
    [int]$Mode,
    [Parameter(Mandatory = $true)][int]$SourceX,
    [Parameter(Mandatory = $true)][int]$SourceY,
    [Parameter(Mandatory = $true)][int]$Width,
    [Parameter(Mandatory = $true)][int]$Height,
    [Parameter(Mandatory = $true)][int]$ClutX,
    [Parameter(Mandatory = $true)][int]$ClutY,
    [Parameter(Mandatory = $true)][ValidateNotNullOrEmpty()][string]$OutputPath,
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$vramWidth = 1024
$vramHeight = 512
$vramHeaderSize = 20
$multiUploadMarker = [uint32]0x20

function Assert-VrmRange {
    param([byte[]]$Bytes, [long]$Offset, [long]$Length, [string]$What)
    if ($Offset -lt 0 -or $Length -lt 0 -or $Offset -gt $Bytes.LongLength -or
        $Length -gt ($Bytes.LongLength - $Offset)) {
        throw "$What exceeds the .vrm payload at byte offset $Offset."
    }
}

function Read-VrmUInt32LE {
    param([byte[]]$Bytes, [long]$Offset, [string]$What)
    Assert-VrmRange -Bytes $Bytes -Offset $Offset -Length 4 -What $What
    # PowerShell preserves a [byte]'s width for shifts, so cast before shifting
    # rather than silently losing every high byte.
    return [uint32](([uint32]$Bytes[$Offset]) -bor
                    (([uint32]$Bytes[($Offset + 1)]) -shl 8) -bor
                    (([uint32]$Bytes[($Offset + 2)]) -shl 16) -bor
                    (([uint32]$Bytes[($Offset + 3)]) -shl 24))
}

function Read-VrmInt16LE {
    param([byte[]]$Bytes, [long]$Offset, [string]$What)
    Assert-VrmRange -Bytes $Bytes -Offset $Offset -Length 2 -What $What
    [int]$value = ([int]$Bytes[$Offset]) -bor (([int]$Bytes[($Offset + 1)]) -shl 8)
    if ($value -ge 32768) { $value -= 65536 }
    return $value
}

function Copy-VrmUpload {
    param(
        [byte[]]$Bytes,
        [long]$HeaderOffset,
        [long]$StoredSize,
        [uint16[]]$Vram,
        [string]$Label
    )

    if ($StoredSize -lt $vramHeaderSize) {
        throw "$Label has a stored size smaller than VramHeader."
    }
    Assert-VrmRange -Bytes $Bytes -Offset $HeaderOffset -Length $StoredSize -What $Label
    [int]$x = Read-VrmInt16LE -Bytes $Bytes -Offset ($HeaderOffset + 12) -What "$Label RECT.x"
    [int]$y = Read-VrmInt16LE -Bytes $Bytes -Offset ($HeaderOffset + 14) -What "$Label RECT.y"
    [int]$width = Read-VrmInt16LE -Bytes $Bytes -Offset ($HeaderOffset + 16) -What "$Label RECT.w"
    [int]$height = Read-VrmInt16LE -Bytes $Bytes -Offset ($HeaderOffset + 18) -What "$Label RECT.h"
    if ($x -lt 0 -or $y -lt 0 -or $width -le 0 -or $height -le 0 -or
        $x -gt $vramWidth -or $y -gt $vramHeight -or $width -gt ($vramWidth - $x) -or $height -gt ($vramHeight - $y)) {
        throw "$Label has an invalid PSX VRAM RECT ($x, $y, $width, $height)."
    }

    [long]$pixelBytes = [long]$width * [long]$height * 2
    if ($StoredSize -lt ($vramHeaderSize + $pixelBytes)) {
        throw "$Label stores $StoredSize bytes but its RECT needs $pixelBytes pixel bytes."
    }
    # In a multi-upload stream the size advances to the next size word.  Any
    # surplus is four-byte alignment padding, never pixel data.
    if (($StoredSize - $vramHeaderSize - $pixelBytes) -gt 3) {
        throw "$Label has more than three bytes of trailing alignment padding."
    }

    [long]$pixelOffset = $HeaderOffset + $vramHeaderSize
    for ([int]$row = 0; $row -lt $height; $row++) {
        [long]$destinationByteOffset = (([long]($y + $row) * $vramWidth) + $x) * 2
        [long]$sourceByteOffset = $pixelOffset + ([long]$row * $width * 2)
        [System.Buffer]::BlockCopy($Bytes, $sourceByteOffset, $Vram, $destinationByteOffset, $width * 2)
    }
}

function Import-VrmPayload {
    param([string]$Path, [uint16[]]$Vram)

    $resolvedPath = (Resolve-Path -LiteralPath $Path -ErrorAction Stop).Path
    if (-not (Test-Path -LiteralPath $resolvedPath -PathType Leaf)) {
        throw "VRM input is not a file: $resolvedPath"
    }
    $bytes = [IO.File]::ReadAllBytes($resolvedPath)
    Assert-VrmRange -Bytes $bytes -Offset 0 -Length 4 -What "VRM header in $resolvedPath"
    [uint32]$firstWord = Read-VrmUInt32LE -Bytes $bytes -Offset 0 -What "VRM header in $resolvedPath"
    if ($firstWord -ne $multiUploadMarker) {
        Copy-VrmUpload -Bytes $bytes -HeaderOffset 0 -StoredSize $bytes.LongLength -Vram $Vram -Label "Single upload in $resolvedPath"
        return
    }

    [long]$sizeFieldOffset = 4
    [int]$uploadIndex = 0
    while ($true) {
        [uint32]$storedSize32 = Read-VrmUInt32LE -Bytes $bytes -Offset $sizeFieldOffset -What "Upload $uploadIndex size in $resolvedPath"
        if ($storedSize32 -eq 0) { break }
        [long]$storedSize = $storedSize32
        if (($storedSize % 4) -ne 0) {
            throw "Upload $uploadIndex in $resolvedPath has non-four-byte-aligned stored size $storedSize."
        }
        [long]$headerOffset = $sizeFieldOffset + 4
        Copy-VrmUpload -Bytes $bytes -HeaderOffset $headerOffset -StoredSize $storedSize -Vram $Vram -Label "Upload $uploadIndex in $resolvedPath"
        $sizeFieldOffset = $headerOffset + $storedSize
        $uploadIndex++
    }
}

function Get-PsxRgba {
    param([uint16]$Color, [int]$PaletteIndex = -1)
    [int]$rgb = $Color -band 0x7fff
    if ($PaletteIndex -eq 0 -or $rgb -eq 0) {
        return [byte[]](0, 0, 0, 0)
    }
    [int]$red5 = $rgb -band 31
    [int]$green5 = ($rgb -shr 5) -band 31
    [int]$blue5 = ($rgb -shr 10) -band 31
    # STP marks a semi-transparent color.  A PNG has no PSX blend mode, so
    # retain that distinction as 50% alpha; ordinary opaque palette entries
    # are alpha 255.  Indexed zero and RGB zero are always transparent.
    [byte]$alpha = if (($Color -band 0x8000) -ne 0) { 128 } else { 255 }
    return [byte[]](
        (($red5 -shl 3) -bor ($red5 -shr 2)),
        (($green5 -shl 3) -bor ($green5 -shr 2)),
        (($blue5 -shl 3) -bor ($blue5 -shr 2)),
        $alpha
    )
}

if ($SourceX -lt 0 -or $SourceY -lt 0 -or $Width -le 0 -or $Height -le 0 -or
    $SourceX -gt $vramWidth -or $SourceY -gt $vramHeight -or
    $Width -gt ($vramWidth - $SourceX) -or $Height -gt ($vramHeight - $SourceY)) {
    throw "Source rectangle is outside 1024x512 PSX VRAM: ($SourceX, $SourceY, $Width, $Height)."
}
if ($ClutX -lt 0 -or $ClutY -lt 0 -or $ClutX -ge $vramWidth -or $ClutY -ge $vramHeight) {
    throw "CLUT origin is outside 1024x512 PSX VRAM: ($ClutX, $ClutY)."
}
[int]$texelsPerWord = switch ($Mode) { 4 { 4 } 8 { 2 } 16 { 1 } }
[int]$paletteSize = if ($Mode -eq 4) { 16 } elseif ($Mode -eq 8) { 256 } else { 0 }
if ($paletteSize -ne 0 -and $ClutX -gt ($vramWidth - $paletteSize)) {
    throw "Mode $Mode CLUT at x=$ClutX needs $paletteSize contiguous VRAM words."
}

$fullOutputPath = [IO.Path]::GetFullPath($OutputPath)
if (-not [IO.Path]::GetExtension($fullOutputPath).Equals('.png', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputPath must name a .png file.'
}
$outputParent = Split-Path -Parent $fullOutputPath
if (-not (Test-Path -LiteralPath $outputParent -PathType Container)) {
    throw "Output directory does not exist: $outputParent"
}
if ((Test-Path -LiteralPath $fullOutputPath) -and -not $Force) {
    throw "Output file already exists: $fullOutputPath (pass -Force to overwrite it)."
}

$vram = [uint16[]]::new($vramWidth * $vramHeight)
foreach ($path in $VrmPath) {
    Import-VrmPayload -Path $path -Vram $vram
}

[int]$outputWidth = $Width * $texelsPerWord
[int]$outputHeight = $Height
Add-Type -AssemblyName System.Drawing
$bitmap = $null
$bitmapData = $null
try {
    $bitmap = [Drawing.Bitmap]::new($outputWidth, $outputHeight, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $rgba = [byte[]]::new($outputWidth * $outputHeight * 4)
    for ([int]$row = 0; $row -lt $Height; $row++) {
        for ([int]$wordColumn = 0; $wordColumn -lt $Width; $wordColumn++) {
            [uint16]$packed = $vram[(($SourceY + $row) * $vramWidth) + $SourceX + $wordColumn]
            for ([int]$texelInWord = 0; $texelInWord -lt $texelsPerWord; $texelInWord++) {
                [int]$paletteIndex = -1
                [uint16]$color = $packed
                if ($Mode -eq 4) {
                    $paletteIndex = ($packed -shr ($texelInWord * 4)) -band 15
                    $color = $vram[($ClutY * $vramWidth) + $ClutX + $paletteIndex]
                }
                elseif ($Mode -eq 8) {
                    $paletteIndex = ($packed -shr ($texelInWord * 8)) -band 255
                    $color = $vram[($ClutY * $vramWidth) + $ClutX + $paletteIndex]
                }
                $colorRgba = Get-PsxRgba -Color $color -PaletteIndex $paletteIndex
                [int]$outputColumn = $wordColumn * $texelsPerWord + $texelInWord
                [int]$offset = ($row * $outputWidth + $outputColumn) * 4
                # Format32bppArgb is BGRA in memory on Windows.
                $rgba[$offset] = $colorRgba[2]
                $rgba[$offset + 1] = $colorRgba[1]
                $rgba[$offset + 2] = $colorRgba[0]
                $rgba[$offset + 3] = $colorRgba[3]
            }
        }
    }
    $bounds = [Drawing.Rectangle]::new(0, 0, $outputWidth, $outputHeight)
    $bitmapData = $bitmap.LockBits($bounds, [Drawing.Imaging.ImageLockMode]::WriteOnly, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
    if ($bitmapData.Stride -ne ($outputWidth * 4)) {
        throw "Unexpected PNG bitmap stride $($bitmapData.Stride) for width $outputWidth."
    }
    [Runtime.InteropServices.Marshal]::Copy($rgba, 0, $bitmapData.Scan0, $rgba.Length)
    $bitmap.UnlockBits($bitmapData)
    $bitmapData = $null
    $bitmap.Save($fullOutputPath, [Drawing.Imaging.ImageFormat]::Png)
    Write-Host "[CTR VRM Rectangle] wrote $outputWidth`x$outputHeight mode-$Mode PNG to $fullOutputPath"
}
finally {
    if ($null -ne $bitmapData) { $bitmap.UnlockBits($bitmapData) }
    if ($null -ne $bitmap) { $bitmap.Dispose() }
}
