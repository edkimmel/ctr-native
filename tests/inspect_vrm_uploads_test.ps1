[CmdletBinding()]
param(
    [string]$InspectorPath = (Join-Path $PSScriptRoot '..\tools\inspect-vrm-uploads.ps1')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$resolvedInspectorPath = (Resolve-Path -LiteralPath $InspectorPath -ErrorAction Stop).Path
$testDirectory = Join-Path ([IO.Path]::GetTempPath()) ('ctr-native-inspect-vrm-uploads-test-' + [Guid]::NewGuid().ToString('N'))
$vrmPath = Join-Path $testDirectory 'fixture.vrm'
$outputPath = Join-Path $testDirectory 'fixture.csv'
$badPath = Join-Path $testDirectory 'bad.vrm'

function Add-UInt32LE {
    param([System.Collections.Generic.List[byte]]$Bytes, [uint32]$Value)
    0..3 | ForEach-Object { $Bytes.Add([byte](($Value -shr (8 * $_)) -band 0xff)) }
}

function Add-Int16LE {
    param([System.Collections.Generic.List[byte]]$Bytes, [int]$Value)
    [uint16]$bits = [uint16]$Value
    $Bytes.Add([byte]($bits -band 0xff))
    $Bytes.Add([byte](($bits -shr 8) -band 0xff))
}

function Add-Upload {
    param([System.Collections.Generic.List[byte]]$Bytes, [int]$X, [int]$Y, [int]$Width, [int]$Height)
    [uint32]$storedSize = 20 + (2 * $Width * $Height)
    Add-UInt32LE -Bytes $Bytes -Value $storedSize
    # First 12 VramHeader bytes are opaque TIM data to the loader.
    1..12 | ForEach-Object { $Bytes.Add(0) }
    Add-Int16LE -Bytes $Bytes -Value $X
    Add-Int16LE -Bytes $Bytes -Value $Y
    Add-Int16LE -Bytes $Bytes -Value $Width
    Add-Int16LE -Bytes $Bytes -Value $Height
    1..($storedSize - 20) | ForEach-Object { $Bytes.Add(0) }
}

try {
    [IO.Directory]::CreateDirectory($testDirectory) | Out-Null
    $fixture = [System.Collections.Generic.List[byte]]::new()
    Add-UInt32LE -Bytes $fixture -Value 0x20
    Add-Upload -Bytes $fixture -X 64 -Y 0 -Width 4 -Height 2
    Add-Upload -Bytes $fixture -X 512 -Y 256 -Width 8 -Height 1
    Add-UInt32LE -Bytes $fixture -Value 0
    [IO.File]::WriteAllBytes($vrmPath, $fixture.ToArray())

    $rows = @(& $resolvedInspectorPath -VrmPath $vrmPath | ConvertFrom-Csv)
    if ($rows.Count -ne 2 -or $rows[0].input_offset -ne '8' -or $rows[0].stored_size -ne '36' -or
        $rows[0].x -ne '64' -or $rows[0].height -ne '2' -or $rows[0].pixel_byte_count -ne '16' -or
        $rows[1].input_offset -ne '48' -or $rows[1].data_offset -ne '68' -or $rows[1].pixel_byte_count -ne '16') {
        throw 'Multi-upload inventory did not report the expected exact offsets and rectangle data.'
    }

    & $resolvedInspectorPath -VrmPath $vrmPath -OutputPath $outputPath
    $existingRejected = $false
    try {
        & $resolvedInspectorPath -VrmPath $vrmPath -OutputPath $outputPath
    }
    catch {
        $existingRejected = $_.Exception.Message -match 'already exists'
    }
    if (-not $existingRejected) {
        throw 'Existing output was overwritten without -Force.'
    }
    & $resolvedInspectorPath -VrmPath $vrmPath -OutputPath $outputPath -Force -Format Json
    if ((Get-Content -LiteralPath $outputPath -Raw | ConvertFrom-Json).Count -ne 2) {
        throw 'Forced JSON output did not contain both uploads.'
    }

    [IO.File]::WriteAllBytes($badPath, [byte[]](0x20, 0, 0, 0, 20, 0, 0, 0))
    $badRejected = $false
    try {
        & $resolvedInspectorPath -VrmPath $badPath | Out-Null
    }
    catch {
        $badRejected = $_.Exception.Message -match 'exceeds the .vrm payload'
    }
    if (-not $badRejected) {
        throw 'Truncated upload was accepted.'
    }

    Write-Host '[CTR VRM Inventory Test] passed'
}
finally {
    if (Test-Path -LiteralPath $testDirectory) {
        [IO.Directory]::Delete($testDirectory, $true)
    }
}
