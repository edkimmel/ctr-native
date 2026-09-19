[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$VrmPath,

    # When omitted, CSV is written to the pipeline rather than the filesystem.
    [string]$OutputPath,

    [ValidateSet('Csv', 'Json')]
    [string]$Format = 'Csv',

    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# LOAD_VramFileCallback accepts either one VramHeader at byte zero, or this
# multi-upload layout:
#   u32 0x20; repeat { u32 storedSize; VramHeader header; u8 pixels[...] }; u32 0
# The callback advances from header by (storedSize & ~3).  Retail entries are
# four-byte aligned; reject a non-aligned size rather than guessing how its
# trailing bytes should be interpreted.
$vramHeaderSize = 20
$multiUploadMarker = [uint32]0x20

function Assert-Range {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Bytes,
        [Parameter(Mandatory = $true)][long]$Offset,
        [Parameter(Mandatory = $true)][long]$Length,
        [Parameter(Mandatory = $true)][string]$What
    )

    if ($Offset -lt 0 -or $Length -lt 0 -or $Offset -gt $Bytes.LongLength -or
        $Length -gt ($Bytes.LongLength - $Offset)) {
        throw "$What exceeds the .vrm payload at byte offset $Offset."
    }
}

function Read-UInt32LE {
    param([byte[]]$Bytes, [long]$Offset, [string]$What)
    Assert-Range -Bytes $Bytes -Offset $Offset -Length 4 -What $What
    return [uint32](($Bytes[$Offset]) -bor
                    (($Bytes[($Offset + 1)]) -shl 8) -bor
                    (($Bytes[($Offset + 2)]) -shl 16) -bor
                    (($Bytes[($Offset + 3)]) -shl 24))
}

function Read-Int16LE {
    param([byte[]]$Bytes, [long]$Offset, [string]$What)
    Assert-Range -Bytes $Bytes -Offset $Offset -Length 2 -What $What
    [int]$value = $Bytes[$Offset] -bor (($Bytes[($Offset + 1)]) -shl 8)
    if ($value -ge 32768) {
        $value -= 65536
    }
    return $value
}

function Read-VramUpload {
    param(
        [Parameter(Mandatory = $true)][byte[]]$Bytes,
        [Parameter(Mandatory = $true)][long]$HeaderOffset,
        [Parameter(Mandatory = $true)][long]$StoredSize,
        [Parameter(Mandatory = $true)][int]$UploadIndex,
        [long]$SizeFieldOffset = -1
    )

    if ($StoredSize -lt $vramHeaderSize) {
        throw "Upload $UploadIndex at byte offset $HeaderOffset has stored size $StoredSize, smaller than VramHeader ($vramHeaderSize)."
    }
    if (($StoredSize % 4) -ne 0) {
        throw "Upload $UploadIndex at byte offset $HeaderOffset has non-four-byte-aligned stored size $StoredSize."
    }
    Assert-Range -Bytes $Bytes -Offset $HeaderOffset -Length $StoredSize -What "Upload $UploadIndex"

    # struct VramHeader has 12 bytes of TIM data, then RECT at +12.
    [int]$x = Read-Int16LE -Bytes $Bytes -Offset ($HeaderOffset + 12) -What "Upload $UploadIndex RECT.x"
    [int]$y = Read-Int16LE -Bytes $Bytes -Offset ($HeaderOffset + 14) -What "Upload $UploadIndex RECT.y"
    [int]$width = Read-Int16LE -Bytes $Bytes -Offset ($HeaderOffset + 16) -What "Upload $UploadIndex RECT.w"
    [int]$height = Read-Int16LE -Bytes $Bytes -Offset ($HeaderOffset + 18) -What "Upload $UploadIndex RECT.h"

    if ($x -lt 0 -or $y -lt 0 -or $width -le 0 -or $height -le 0 -or
        $x -gt 1024 -or $y -gt 512 -or $width -gt (1024 - $x) -or $height -gt (512 - $y)) {
        throw "Upload $UploadIndex at byte offset $HeaderOffset has invalid PSX VRAM RECT ($x, $y, $width, $height)."
    }

    [long]$pixelByteCount = $StoredSize - $vramHeaderSize
    [long]$expectedPixelByteCount = [long]$width * [long]$height * 2
    if ($pixelByteCount -ne $expectedPixelByteCount) {
        throw "Upload $UploadIndex at byte offset $HeaderOffset stores $pixelByteCount pixel bytes; RECT requires $expectedPixelByteCount."
    }

    [pscustomobject][ordered]@{
        upload_index     = $UploadIndex
        input_offset     = $HeaderOffset
        size_field_offset = $SizeFieldOffset
        stored_size      = $StoredSize
        data_offset      = $HeaderOffset + $vramHeaderSize
        x                = $x
        y                = $y
        width            = $width
        height           = $height
        pixel_byte_count = $pixelByteCount
    }
}

$resolvedVrmPath = (Resolve-Path -LiteralPath $VrmPath -ErrorAction Stop).Path
if (-not (Test-Path -LiteralPath $resolvedVrmPath -PathType Leaf)) {
    throw "VRM input is not a file: $resolvedVrmPath"
}
$bytes = [System.IO.File]::ReadAllBytes($resolvedVrmPath)
Assert-Range -Bytes $bytes -Offset 0 -Length 4 -What 'VRM header'

$rows = [System.Collections.Generic.List[object]]::new()
[uint32]$firstWord = Read-UInt32LE -Bytes $bytes -Offset 0 -What 'VRM header'
if ($firstWord -ne $multiUploadMarker) {
    $rows.Add((Read-VramUpload -Bytes $bytes -HeaderOffset 0 -StoredSize $bytes.LongLength -UploadIndex 0))
}
else {
    [long]$sizeFieldOffset = 4
    [int]$uploadIndex = 0
    while ($true) {
        [uint32]$storedSize32 = Read-UInt32LE -Bytes $bytes -Offset $sizeFieldOffset -What "Upload $uploadIndex size"
        if ($storedSize32 -eq 0) {
            break
        }
        [long]$storedSize = $storedSize32
        [long]$headerOffset = $sizeFieldOffset + 4
        $rows.Add((Read-VramUpload -Bytes $bytes -HeaderOffset $headerOffset -StoredSize $storedSize -UploadIndex $uploadIndex -SizeFieldOffset $sizeFieldOffset))
        $sizeFieldOffset = $headerOffset + $storedSize
        $uploadIndex++
    }
}

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    if ($Format -eq 'Csv') {
        $rows | ConvertTo-Csv -NoTypeInformation
    }
    else {
        $rows | ConvertTo-Json -Depth 3
    }
    return
}

$fullOutputPath = [System.IO.Path]::GetFullPath($OutputPath)
if ([System.StringComparer]::OrdinalIgnoreCase.Equals($fullOutputPath, [System.IO.Path]::GetFullPath($resolvedVrmPath))) {
    throw 'OutputPath must not replace the input .vrm payload.'
}
$outputParent = Split-Path -Parent $fullOutputPath
if (-not (Test-Path -LiteralPath $outputParent -PathType Container)) {
    throw "Output directory does not exist: $outputParent"
}
if ((Test-Path -LiteralPath $fullOutputPath) -and -not $Force) {
    throw "Output file already exists: $fullOutputPath (pass -Force to overwrite it)."
}

[string]$serialized = if ($Format -eq 'Csv') {
    ($rows | ConvertTo-Csv -NoTypeInformation) -join [Environment]::NewLine
}
else {
    $rows | ConvertTo-Json -Depth 3
}
[System.IO.File]::WriteAllText($fullOutputPath, $serialized + [Environment]::NewLine, [System.Text.UTF8Encoding]::new($false))
Write-Host "[CTR VRM Inventory] wrote $($rows.Count) uploads to $fullOutputPath"
