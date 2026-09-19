[CmdletBinding()]
param([string]$ConverterPath = (Join-Path $PSScriptRoot '..\tools\convert-png-to-ctrh.ps1'))

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$resolvedConverterPath = (Resolve-Path -LiteralPath $ConverterPath -ErrorAction Stop).Path
$testDirectory = Join-Path ([IO.Path]::GetTempPath()) ('ctr-native-convert-png-to-ctrh-test-' + [Guid]::NewGuid().ToString('N'))

function Read-U32LE { param([byte[]]$Bytes, [int]$Offset)
    return [uint32](([uint32]$Bytes[$Offset]) -bor (([uint32]$Bytes[$Offset + 1]) -shl 8) -bor (([uint32]$Bytes[$Offset + 2]) -shl 16) -bor (([uint32]$Bytes[$Offset + 3]) -shl 24))
}
function Assert-Rgba { param([byte[]]$Bytes, [int]$Width, [int]$X, [int]$Y, [int]$R, [int]$G, [int]$B, [int]$A)
    [int]$offset = 16 + (($Y * $Width + $X) * 4)
    if ($Bytes[$offset] -ne $R -or $Bytes[$offset + 1] -ne $G -or $Bytes[$offset + 2] -ne $B -or $Bytes[$offset + 3] -ne $A) {
        throw "Unexpected RGBA at ($X,$Y): $($Bytes[$offset]),$($Bytes[$offset + 1]),$($Bytes[$offset + 2]),$($Bytes[$offset + 3])"
    }
}

try {
    [IO.Directory]::CreateDirectory($testDirectory) | Out-Null
    Add-Type -AssemblyName System.Drawing
    $png = Join-Path $testDirectory 'fixture.png'
    $bitmap = [Drawing.Bitmap]::new(3, 3, [Drawing.Imaging.PixelFormat]::Format32bppArgb)
    try {
        # The centre has B=D=red, F=blue and H=green, exercising the genuine
        # edge-aware top-left EPX rule.  The transparent corners prove alpha
        # survives decode, scale, and raw CTRH serialization.
        $transparent = [Drawing.Color]::FromArgb(0, 12, 34, 56)
        $black = [Drawing.Color]::FromArgb(255, 0, 0, 0)
        $red = [Drawing.Color]::FromArgb(255, 255, 0, 0)
        $blue = [Drawing.Color]::FromArgb(255, 0, 0, 255)
        $green = [Drawing.Color]::FromArgb(128, 0, 255, 0)
        $centre = [Drawing.Color]::FromArgb(192, 70, 80, 90)
        $bitmap.SetPixel(0, 0, $transparent); $bitmap.SetPixel(2, 0, $transparent)
        $bitmap.SetPixel(0, 2, $transparent); $bitmap.SetPixel(2, 2, $transparent)
        $bitmap.SetPixel(1, 0, $red); $bitmap.SetPixel(0, 1, $red); $bitmap.SetPixel(2, 1, $blue); $bitmap.SetPixel(1, 2, $green)
        $bitmap.SetPixel(1, 1, $centre)
        $bitmap.Save($png, [Drawing.Imaging.ImageFormat]::Png)
    } finally { $bitmap.Dispose() }

    $out1 = Join-Path $testDirectory 'one.ctrh'
    & $resolvedConverterPath -InputPngPath $png -UpscaleFactor 1 -OutputCtrhPath $out1
    $one = [IO.File]::ReadAllBytes($out1)
    if ([Text.Encoding]::ASCII.GetString($one, 0, 4) -ne 'CTRH' -or (Read-U32LE $one 4) -ne 1 -or (Read-U32LE $one 8) -ne 3 -or (Read-U32LE $one 12) -ne 3 -or $one.Length -ne (16 + 3 * 3 * 4)) { throw 'Scale 1 did not write an exact CTRH v1 header/payload size.' }
    Assert-Rgba $one 3 1 1 70 80 90 192
    Assert-Rgba $one 3 0 0 12 34 56 0

    $out2 = Join-Path $testDirectory 'two.ctrh'
    & $resolvedConverterPath -InputPngPath $png -UpscaleFactor 2 -OutputCtrhPath $out2
    $two = [IO.File]::ReadAllBytes($out2)
    if ((Read-U32LE $two 8) -ne 6 -or (Read-U32LE $two 12) -ne 6 -or $two.Length -ne (16 + 6 * 6 * 4)) { throw 'Scale 2 dimensions or payload size are incorrect.' }
    Assert-Rgba $two 6 2 2 255 0 0 255
    Assert-Rgba $two 6 3 2 70 80 90 192
    Assert-Rgba $two 6 2 3 70 80 90 192
    Assert-Rgba $two 6 3 3 70 80 90 192
    Assert-Rgba $two 6 0 0 12 34 56 0

    $out4 = Join-Path $testDirectory 'four.ctrh'
    & $resolvedConverterPath -InputPngPath $png -UpscaleFactor 4 -OutputCtrhPath $out4
    $four = [IO.File]::ReadAllBytes($out4)
    if ((Read-U32LE $four 8) -ne 12 -or (Read-U32LE $four 12) -ne 12 -or $four.Length -ne (16 + 12 * 12 * 4)) { throw 'Scale 4 did not apply two Scale2x passes.' }
    Assert-Rgba $four 12 0 0 12 34 56 0

    $overwriteRejected = $false
    try { & $resolvedConverterPath -InputPngPath $png -UpscaleFactor 2 -OutputCtrhPath $out2 } catch { $overwriteRejected = $_.Exception.Message -match 'already exists' }
    if (-not $overwriteRejected) { throw 'Existing CTRH was overwritten without -Force.' }
    & $resolvedConverterPath -InputPngPath $png -UpscaleFactor 2 -OutputCtrhPath $out2 -Force
    Write-Host '[CTR Scale2x CTRH Test] passed'
}
finally {
    if (Test-Path -LiteralPath $testDirectory) { [IO.Directory]::Delete($testDirectory, $true) }
}
