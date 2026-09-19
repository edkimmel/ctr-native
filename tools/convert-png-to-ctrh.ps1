[CmdletBinding(DefaultParameterSetName = 'Convert')]
param(
    # A decoded retail texture.  Its colors and alpha become the base CTRH
    # pixels before Scale2x is applied.
    [Parameter(Mandatory = $true, ParameterSetName = 'Convert')]
    [ValidateNotNullOrEmpty()]
    [string]$InputPngPath,

    # CTRH v1 accepts only an exact integer multiple of the traced source
    # rectangle.  Scale2x is applied once per factor-of-two step.
    [Parameter(Mandatory = $true, ParameterSetName = 'Convert')]
    [ValidateSet(1, 2, 4, 8)]
    [int]$UpscaleFactor,

    [Parameter(Mandatory = $true, ParameterSetName = 'Convert')]
    [ValidateNotNullOrEmpty()]
    [string]$OutputCtrhPath,

    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$cthrVersion = [uint32]1
$cthrHeaderBytes = 16
$maxDimension = 4096
$maxRgbaBytes = 64MB

function Test-RgbaEqual {
    param(
        [byte[]]$Pixels,
        [int]$FirstOffset,
        [int]$SecondOffset
    )

    return $Pixels[$FirstOffset] -eq $Pixels[$SecondOffset] -and
           $Pixels[$FirstOffset + 1] -eq $Pixels[$SecondOffset + 1] -and
           $Pixels[$FirstOffset + 2] -eq $Pixels[$SecondOffset + 2] -and
           $Pixels[$FirstOffset + 3] -eq $Pixels[$SecondOffset + 3]
}

function Copy-RgbaPixel {
    param(
        [byte[]]$Source,
        [int]$SourceOffset,
        [byte[]]$Destination,
        [int]$DestinationOffset
    )

    $Destination[$DestinationOffset] = $Source[$SourceOffset]
    $Destination[$DestinationOffset + 1] = $Source[$SourceOffset + 1]
    $Destination[$DestinationOffset + 2] = $Source[$SourceOffset + 2]
    $Destination[$DestinationOffset + 3] = $Source[$SourceOffset + 3]
}

function Invoke-Scale2xRgba {
    param(
        [byte[]]$Pixels,
        [int]$Width,
        [int]$Height
    )

    [long]$nextWidthLong = [long]$Width * 2
    [long]$nextHeightLong = [long]$Height * 2
    [long]$nextByteCount = $nextWidthLong * $nextHeightLong * 4
    if ($nextWidthLong -gt [int]::MaxValue -or $nextHeightLong -gt [int]::MaxValue -or
        $nextByteCount -gt [int]::MaxValue) {
        throw 'Scale2x output exceeds the PowerShell byte-array limit.'
    }

    [int]$nextWidth = $nextWidthLong
    [int]$nextHeight = $nextHeightLong
    $next = [byte[]]::new([int]$nextByteCount)
    for ([int]$y = 0; $y -lt $Height; $y++) {
        [int]$topY = if ($y -gt 0) { $y - 1 } else { $y }
        [int]$bottomY = if ($y -lt ($Height - 1)) { $y + 1 } else { $y }
        for ([int]$x = 0; $x -lt $Width; $x++) {
            [int]$leftX = if ($x -gt 0) { $x - 1 } else { $x }
            [int]$rightX = if ($x -lt ($Width - 1)) { $x + 1 } else { $x }
            [int]$e = (($y * $Width) + $x) * 4
            [int]$b = (($topY * $Width) + $x) * 4
            [int]$d = (($y * $Width) + $leftX) * 4
            [int]$f = (($y * $Width) + $rightX) * 4
            [int]$h = (($bottomY * $Width) + $x) * 4
            [int]$topLeft = (($y * 2 * $nextWidth) + ($x * 2)) * 4
            [int]$topRight = $topLeft + 4
            [int]$bottomLeft = $topLeft + ($nextWidth * 4)
            [int]$bottomRight = $bottomLeft + 4

            # EPX / Scale2x.  RGBA is the color identity, so a transparent
            # texel cannot leak its RGB into a neighboring opaque texel.
            if ((Test-RgbaEqual $Pixels $d $b) -and -not (Test-RgbaEqual $Pixels $d $h) -and -not (Test-RgbaEqual $Pixels $b $f)) {
                Copy-RgbaPixel $Pixels $d $next $topLeft
            } else { Copy-RgbaPixel $Pixels $e $next $topLeft }
            if ((Test-RgbaEqual $Pixels $b $f) -and -not (Test-RgbaEqual $Pixels $b $d) -and -not (Test-RgbaEqual $Pixels $f $h)) {
                Copy-RgbaPixel $Pixels $f $next $topRight
            } else { Copy-RgbaPixel $Pixels $e $next $topRight }
            if ((Test-RgbaEqual $Pixels $d $h) -and -not (Test-RgbaEqual $Pixels $d $b) -and -not (Test-RgbaEqual $Pixels $h $f)) {
                Copy-RgbaPixel $Pixels $d $next $bottomLeft
            } else { Copy-RgbaPixel $Pixels $e $next $bottomLeft }
            if ((Test-RgbaEqual $Pixels $h $f) -and -not (Test-RgbaEqual $Pixels $d $h) -and -not (Test-RgbaEqual $Pixels $b $f)) {
                Copy-RgbaPixel $Pixels $f $next $bottomRight
            } else { Copy-RgbaPixel $Pixels $e $next $bottomRight }
        }
    }
    return [pscustomobject]@{ Pixels = $next; Width = $nextWidth; Height = $nextHeight }
}

$fullInputPath = (Resolve-Path -LiteralPath $InputPngPath -ErrorAction Stop).Path
if (-not (Test-Path -LiteralPath $fullInputPath -PathType Leaf)) {
    throw "InputPngPath is not a file: $fullInputPath"
}
if (-not [IO.Path]::GetExtension($fullInputPath).Equals('.png', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'InputPngPath must name a decoded .png file.'
}
$fullOutputPath = [IO.Path]::GetFullPath($OutputCtrhPath)
if (-not [IO.Path]::GetExtension($fullOutputPath).Equals('.ctrh', [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputCtrhPath must name a .ctrh file.'
}
if ([string]::Equals($fullInputPath, $fullOutputPath, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'InputPngPath and OutputCtrhPath must be distinct.'
}
$outputParent = [IO.Path]::GetDirectoryName($fullOutputPath)
if (-not (Test-Path -LiteralPath $outputParent -PathType Container)) {
    throw "Output directory does not exist: $outputParent"
}
if ((Test-Path -LiteralPath $fullOutputPath) -and -not $Force) {
    throw "Output file already exists: $fullOutputPath (pass -Force to overwrite it)."
}

Add-Type -AssemblyName System.Drawing
$bitmap = $null
try {
    $bitmap = [Drawing.Bitmap]::new($fullInputPath)
    [int]$sourceWidth = $bitmap.Width
    [int]$sourceHeight = $bitmap.Height
    if ($sourceWidth -le 0 -or $sourceHeight -le 0) { throw 'Input PNG has invalid dimensions.' }
    [long]$outputWidthLong = [long]$sourceWidth * $UpscaleFactor
    [long]$outputHeightLong = [long]$sourceHeight * $UpscaleFactor
    [long]$rgbaByteCount = $outputWidthLong * $outputHeightLong * 4
    if ($outputWidthLong -gt $maxDimension -or $outputHeightLong -gt $maxDimension -or
        $rgbaByteCount -gt $maxRgbaBytes) {
        throw "Scaled dimensions $outputWidthLong`x$outputHeightLong exceed CTRH v1's 4096x4096 / 64 MiB bounds."
    }

    $pixels = [byte[]]::new($sourceWidth * $sourceHeight * 4)
    for ([int]$y = 0; $y -lt $sourceHeight; $y++) {
        for ([int]$x = 0; $x -lt $sourceWidth; $x++) {
            $color = $bitmap.GetPixel($x, $y)
            [int]$offset = (($y * $sourceWidth) + $x) * 4
            $pixels[$offset] = $color.R
            $pixels[$offset + 1] = $color.G
            $pixels[$offset + 2] = $color.B
            $pixels[$offset + 3] = $color.A
        }
    }
} finally {
    if ($null -ne $bitmap) { $bitmap.Dispose() }
}

[int]$width = $sourceWidth
[int]$height = $sourceHeight
[int]$remainingPasses = switch ($UpscaleFactor) { 1 { 0 } 2 { 1 } 4 { 2 } 8 { 3 } }
for ([int]$pass = 0; $pass -lt $remainingPasses; $pass++) {
    $scaled = Invoke-Scale2xRgba -Pixels $pixels -Width $width -Height $height
    $pixels = $scaled.Pixels
    $width = $scaled.Width
    $height = $scaled.Height
}
if ($width -ne $outputWidthLong -or $height -ne $outputHeightLong -or $pixels.LongLength -ne $rgbaByteCount) {
    throw 'Internal Scale2x output size mismatch.'
}

$outputBytes = [byte[]]::new($cthrHeaderBytes + [int]$pixels.LongLength)
$outputBytes[0] = [byte][char]'C'; $outputBytes[1] = [byte][char]'T'; $outputBytes[2] = [byte][char]'R'; $outputBytes[3] = [byte][char]'H'
for ([int]$byteIndex = 0; $byteIndex -lt 4; $byteIndex++) {
    $outputBytes[4 + $byteIndex] = [byte](($cthrVersion -shr ($byteIndex * 8)) -band 255)
    $outputBytes[8 + $byteIndex] = [byte](([uint32]$width -shr ($byteIndex * 8)) -band 255)
    $outputBytes[12 + $byteIndex] = [byte](([uint32]$height -shr ($byteIndex * 8)) -band 255)
}
[Buffer]::BlockCopy($pixels, 0, $outputBytes, $cthrHeaderBytes, $pixels.Length)
[IO.File]::WriteAllBytes($fullOutputPath, $outputBytes)
Write-Host "[CTR Scale2x CTRH] wrote $width`x$height ($UpscaleFactor`x) to $fullOutputPath"
