[CmdletBinding()]
param([string]$ExporterPath = (Join-Path $PSScriptRoot '..\tools\export-vrm-rectangle.ps1'))

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$resolvedExporterPath = (Resolve-Path -LiteralPath $ExporterPath -ErrorAction Stop).Path
$testDirectory = Join-Path ([IO.Path]::GetTempPath()) ('ctr-native-export-vrm-rectangle-test-' + [Guid]::NewGuid().ToString('N'))
$vrmPath = Join-Path $testDirectory 'fixture.vrm'

function Add-U32 { param($B, [uint32]$V) 0..3 | % { $B.Add([byte](($V -shr (8 * $_)) -band 255)) } }
function Add-U16 { param($B, [uint16]$V) $B.Add([byte]($V -band 255)); $B.Add([byte](($V -shr 8) -band 255)) }
function Add-Upload {
    param($B, [int]$X, [int]$Y, [int]$W, [int]$H, [uint16[]]$Pixels)
    [int]$rawBytes = $W * $H * 2
    [int]$storedSize = 20 + $rawBytes
    while (($storedSize % 4) -ne 0) { $storedSize++ }
    Add-U32 $B $storedSize
    1..12 | % { $B.Add(0) }
    Add-U16 $B ([uint16]$X); Add-U16 $B ([uint16]$Y); Add-U16 $B ([uint16]$W); Add-U16 $B ([uint16]$H)
    foreach ($pixel in $Pixels) { Add-U16 $B $pixel }
    [int]$padding = $storedSize - 20 - $rawBytes
    if ($padding -gt 0) { 1..$padding | % { $B.Add(0) } }
}
function Assert-Pixel { param($Bitmap, [int]$X, [int]$Y, [int]$R, [int]$G, [int]$B, [int]$A)
    $p = $Bitmap.GetPixel($X, $Y)
    if ($p.R -ne $R -or $p.G -ne $G -or $p.B -ne $B -or $p.A -ne $A) { throw "Unexpected pixel ($X,$Y): $($p.R),$($p.G),$($p.B),$($p.A)" }
}
function Assert-Dimensions { param($Bitmap, [int]$Width, [int]$Height)
    if ($Bitmap.Width -ne $Width -or $Bitmap.Height -ne $Height) { throw "Unexpected dimensions: $($Bitmap.Width)x$($Bitmap.Height), expected $Width`x$Height" }
}

try {
    [IO.Directory]::CreateDirectory($testDirectory) | Out-Null
    $b = [Collections.Generic.List[byte]]::new(); Add-U32 $b 0x20
    $pal4 = [uint16[]]::new(16); $pal4[1] = 0x001f; $pal4[2] = 0x83e0; $pal4[3] = 0x7c00
    Add-Upload $b 16 0 16 1 $pal4
    Add-Upload $b 0 0 1 1 ([uint16[]](0x3210))
    $pal8 = [uint16[]]::new(256); $pal8[1] = 0x001f; $pal8[2] = 0x7c00
    Add-Upload $b 0 1 256 1 $pal8
    Add-Upload $b 0 2 1 1 ([uint16[]](0x0201))
    Add-Upload $b 0 3 2 1 ([uint16[]](0x001f, 0x8000))
    Add-U32 $b 0; [IO.File]::WriteAllBytes($vrmPath, $b.ToArray())

    $out4 = Join-Path $testDirectory 'mode4.png'; & $resolvedExporterPath -VrmPath $vrmPath -Mode 4 -SourceX 0 -SourceY 0 -Width 1 -Height 1 -ClutX 16 -ClutY 0 -OutputPath $out4
    $bmp = [Drawing.Bitmap]::new($out4); try { Assert-Dimensions $bmp 4 1; Assert-Pixel $bmp 0 0 0 0 0 0; Assert-Pixel $bmp 1 0 255 0 0 255; Assert-Pixel $bmp 2 0 0 255 0 128; Assert-Pixel $bmp 3 0 0 0 255 255 } finally { $bmp.Dispose() }
    $out8 = Join-Path $testDirectory 'mode8.png'; & $resolvedExporterPath -VrmPath $vrmPath -Mode 8 -SourceX 0 -SourceY 2 -Width 1 -Height 1 -ClutX 0 -ClutY 1 -OutputPath $out8
    $bmp = [Drawing.Bitmap]::new($out8); try { Assert-Dimensions $bmp 2 1; Assert-Pixel $bmp 0 0 255 0 0 255; Assert-Pixel $bmp 1 0 0 0 255 255 } finally { $bmp.Dispose() }
    $out16 = Join-Path $testDirectory 'mode16.png'; & $resolvedExporterPath -VrmPath $vrmPath -Mode 16 -SourceX 0 -SourceY 3 -Width 2 -Height 1 -ClutX 0 -ClutY 0 -OutputPath $out16
    $bmp = [Drawing.Bitmap]::new($out16); try { Assert-Dimensions $bmp 2 1; Assert-Pixel $bmp 0 0 255 0 0 255; Assert-Pixel $bmp 1 0 0 0 0 } finally { $bmp.Dispose() }
    $overwriteRejected = $false; try { & $resolvedExporterPath -VrmPath $vrmPath -Mode 4 -SourceX 0 -SourceY 0 -Width 1 -Height 1 -ClutX 16 -ClutY 0 -OutputPath $out4 } catch { $overwriteRejected = $_.Exception.Message -match 'already exists' }
    if (-not $overwriteRejected) { throw 'Existing PNG was overwritten without -Force.' }
    Write-Host '[CTR VRM Rectangle Test] passed'
}
finally { if (Test-Path -LiteralPath $testDirectory) { [IO.Directory]::Delete($testDirectory, $true) } }
