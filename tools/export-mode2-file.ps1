[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$BinPath,

    [Parameter(Mandatory = $true)]
    [ValidateRange(0, [UInt32]::MaxValue)]
    [UInt32]$Lba,

    [Parameter(Mandatory = $true)]
    [ValidateRange(1, [UInt32]::MaxValue)]
    [UInt32]$ByteCount,

    [Parameter(Mandatory = $true)]
    [string]$OutputPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# CTR Native's retail NTSC-U image is one raw MODE2/2352 data track. Form-1
# payload begins at byte 24 of each sector and is 2048 bytes long.
$rawSectorSize = 2352
$payloadOffset = 24
$payloadSize = 2048
$resolvedBin = (Resolve-Path -LiteralPath $BinPath -ErrorAction Stop).Path
$resolvedOutput = [IO.Path]::GetFullPath($OutputPath)

if (Test-Path -LiteralPath $resolvedOutput) {
    throw "Refusing to overwrite existing output: $resolvedOutput"
}

$outputDirectory = Split-Path -Parent $resolvedOutput
if (-not (Test-Path -LiteralPath $outputDirectory -PathType Container)) {
    throw "Output directory does not exist: $outputDirectory"
}

$input = [IO.File]::Open($resolvedBin, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
$output = $null
try {
    $startOffset = ([Int64]$Lba * $rawSectorSize)
    if ($startOffset -gt $input.Length) {
        throw "LBA $Lba begins beyond the source image."
    }
    $input.Position = $startOffset
    $output = [IO.File]::Open($resolvedOutput, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
    $sector = New-Object byte[] $rawSectorSize
    [UInt64]$remaining = $ByteCount

    while ($remaining -gt 0) {
        $read = 0
        while ($read -lt $rawSectorSize) {
            $count = $input.Read($sector, $read, $rawSectorSize - $read)
            if ($count -eq 0) { throw 'Unexpected end of source image.' }
            $read += $count
        }
        if ($sector[0] -ne 0 -or $sector[11] -ne 0 -or $sector[15] -ne 2) {
            throw 'Source sector is not expected raw MODE2 data.'
        }
        $copyCount = [Math]::Min([UInt64]$payloadSize, $remaining)
        $output.Write($sector, $payloadOffset, [Int32]$copyCount)
        $remaining -= $copyCount
    }
}
finally {
    if ($null -ne $output) { $output.Dispose() }
    $input.Dispose()
}

Write-Host "[CTR MODE2 Export] lba=$Lba bytes=$ByteCount output=$resolvedOutput"
