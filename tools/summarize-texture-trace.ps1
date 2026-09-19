[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$TracePath,

    [string]$OutputPath,

    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$requiredColumns = @(
    'sequence', 'frame', 'event', 'hazard_mask', 'hazards',
    'tpage', 'tex_format', 'page_x', 'page_y', 'page_w', 'page_h',
    'clut', 'clut_x', 'clut_y',
    'uv_min_u', 'uv_min_v', 'uv_max_u', 'uv_max_v',
    'source_x', 'source_y', 'source_w', 'source_h',
    'destination_x', 'destination_y', 'destination_w', 'destination_h'
)

$sourceKeyColumns = @(
    'tpage', 'tex_format', 'page_x', 'page_y', 'page_w', 'page_h',
    'clut', 'clut_x', 'clut_y',
    'uv_min_u', 'uv_min_v', 'uv_max_u', 'uv_max_v',
    'source_x', 'source_y', 'source_w', 'source_h'
)

# A texture primitive normally writes its destination draw page.  That is not
# a mutation of its sampled source, so it is not independently disqualifying.
# Feedback/overlap on the primitive itself is.  Source mutations and readbacks
# are joined below by exact VRAM rectangle and only after first use.
$directUnsafeHazards = @(
    'framebuffer-overlap',
    'framebuffer-feedback',
    'move-image',
    'store-image',
    'load-image',
    'clear-image',
    'framebuffer-store'
)

$traceCellSize = 64
$script:traceAccessIndex = @{}
$script:traceAccessNextId = 0

function Test-TraceRectValid {
    param([int]$X, [int]$Y, [int]$Width, [int]$Height)
    return $X -ge 0 -and $Y -ge 0 -and $Width -gt 0 -and $Height -gt 0
}

function Test-TraceRectOverlap {
    param(
        [int]$AX, [int]$AY, [int]$AWidth, [int]$AHeight,
        [int]$BX, [int]$BY, [int]$BWidth, [int]$BHeight
    )
    return $AX -lt ($BX + $BWidth) -and $BX -lt ($AX + $AWidth) -and
           $AY -lt ($BY + $BHeight) -and $BY -lt ($AY + $AHeight)
}

function Get-TraceRectCells {
    param([int]$X, [int]$Y, [int]$Width, [int]$Height)
    if (-not (Test-TraceRectValid $X $Y $Width $Height)) {
        return
    }

    $clampedX0 = [Math]::Max(0, $X)
    $clampedY0 = [Math]::Max(0, $Y)
    $clampedX1 = [Math]::Min(1023, $X + $Width - 1)
    $clampedY1 = [Math]::Min(511, $Y + $Height - 1)
    if ($clampedX0 -gt $clampedX1 -or $clampedY0 -gt $clampedY1) {
        return
    }

    for ($cellY = [int]($clampedY0 / $traceCellSize); $cellY -le [int]($clampedY1 / $traceCellSize); $cellY++) {
        for ($cellX = [int]($clampedX0 / $traceCellSize); $cellX -le [int]($clampedX1 / $traceCellSize); $cellX++) {
            "$cellX,$cellY"
        }
    }
}

function Add-TraceAccess {
    param(
        [psobject]$Row,
        [string]$Kind,
        [int]$X, [int]$Y, [int]$Width, [int]$Height
    )
    if (-not (Test-TraceRectValid $X $Y $Width $Height)) {
        return
    }

    $access = [pscustomobject]@{
        id = $script:traceAccessNextId
        sequence = [long]$Row.sequence
        kind = $Kind
        x = $X
        y = $Y
        width = $Width
        height = $Height
    }
    $script:traceAccessNextId++
    foreach ($cell in (Get-TraceRectCells $X $Y $Width $Height)) {
        if (-not $script:traceAccessIndex.ContainsKey($cell)) {
            $script:traceAccessIndex[$cell] = [System.Collections.ArrayList]::new()
        }
        [void]$script:traceAccessIndex[$cell].Add($access)
    }
}

function Get-PostUseTraceAccesses {
    param([long]$FirstUseSequence, [int]$X, [int]$Y, [int]$Width, [int]$Height)
    $seenIds = @{}
    $matches = [System.Collections.ArrayList]::new()
    foreach ($cell in (Get-TraceRectCells $X $Y $Width $Height)) {
        if (-not $script:traceAccessIndex.ContainsKey($cell)) {
            continue
        }
        foreach ($access in $script:traceAccessIndex[$cell]) {
            if ($seenIds.ContainsKey($access.id) -or $access.sequence -le $FirstUseSequence) {
                continue
            }
            $seenIds[$access.id] = $true
            if (Test-TraceRectOverlap $X $Y $Width $Height $access.x $access.y $access.width $access.height) {
                [void]$matches.Add($access)
            }
        }
    }
    return $matches
}

$resolvedTracePath = (Resolve-Path -LiteralPath $TracePath -ErrorAction Stop).Path
if (-not (Test-Path -LiteralPath $resolvedTracePath -PathType Leaf)) {
    throw "Texture trace must be a file: $resolvedTracePath"
}

$header = Get-Content -LiteralPath $resolvedTracePath -TotalCount 1
if ([string]::IsNullOrWhiteSpace($header)) {
    throw "Texture trace CSV is empty: $resolvedTracePath"
}

$headerColumns = $header.Split(',')
foreach ($requiredColumn in $requiredColumns) {
    if ($headerColumns -notcontains $requiredColumn) {
        throw "Texture trace CSV is missing required column '$requiredColumn': $resolvedTracePath"
    }
}

if ([string]::IsNullOrWhiteSpace($OutputPath)) {
    $traceDirectory = Split-Path -Parent $resolvedTracePath
    $traceBaseName = [System.IO.Path]::GetFileNameWithoutExtension($resolvedTracePath)
    $OutputPath = Join-Path $traceDirectory ($traceBaseName + '.candidates.csv')
}

$resolvedOutputPath = [System.IO.Path]::GetFullPath($OutputPath)
if ((Test-Path -LiteralPath $resolvedOutputPath) -and (-not $Force)) {
    throw "Refusing to overwrite existing output without -Force: $resolvedOutputPath"
}

$rows = @(Import-Csv -LiteralPath $resolvedTracePath)
$textureRows = @($rows | Where-Object { $_.event -eq 'texture-primitive' })
if ($textureRows.Count -eq 0) {
    throw "Texture trace has no texture-primitive rows: $resolvedTracePath"
}

foreach ($row in $rows) {
    switch ($row.event) {
        'load-image' {
            Add-TraceAccess $row 'post-use-write:load-image' ([int]$row.destination_x) ([int]$row.destination_y) ([int]$row.destination_w) ([int]$row.destination_h)
        }
        'move-image' {
            Add-TraceAccess $row 'post-use-copy-read:move-image' ([int]$row.source_x) ([int]$row.source_y) ([int]$row.source_w) ([int]$row.source_h)
            Add-TraceAccess $row 'post-use-write:move-image' ([int]$row.destination_x) ([int]$row.destination_y) ([int]$row.destination_w) ([int]$row.destination_h)
        }
        'store-image' {
            Add-TraceAccess $row 'post-use-readback:store-image' ([int]$row.source_x) ([int]$row.source_y) ([int]$row.source_w) ([int]$row.source_h)
        }
        'clear-image' {
            Add-TraceAccess $row 'post-use-write:clear-image' ([int]$row.destination_x) ([int]$row.destination_y) ([int]$row.destination_w) ([int]$row.destination_h)
        }
        'framebuffer-store' {
            Add-TraceAccess $row 'post-use-write:framebuffer-store' ([int]$row.destination_x) ([int]$row.destination_y) ([int]$row.destination_w) ([int]$row.destination_h)
        }
        'framebuffer-feedback-prepared' {
            Add-TraceAccess $row 'post-use-feedback:framebuffer-feedback-prepared' ([int]$row.destination_x) ([int]$row.destination_y) ([int]$row.destination_w) ([int]$row.destination_h)
        }
    }
}

$candidates = foreach ($group in ($textureRows | Group-Object -Property $sourceKeyColumns)) {
    $first = $group.Group[0]
    $hazardMasks = @($group.Group | ForEach-Object { $_.hazard_mask } | Sort-Object -Unique)
    $hazardClasses = @($group.Group |
        ForEach-Object { $_.hazards -split '\|' } |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
        Sort-Object -Unique)
    $directUnsafeForKey = @($hazardClasses | Where-Object { $directUnsafeHazards -contains $_ })
    $hazardRows = @($group.Group | Where-Object { $_.hazard_mask -notmatch '^0x0+$' }).Count
    $firstUseSequence = [long](($group.Group | Measure-Object -Property sequence -Minimum).Minimum)
    $sourceValid = Test-TraceRectValid ([int]$first.source_x) ([int]$first.source_y) ([int]$first.source_w) ([int]$first.source_h)
    $postUseAccesses = if ($sourceValid) {
        @(Get-PostUseTraceAccesses $firstUseSequence ([int]$first.source_x) ([int]$first.source_y) ([int]$first.source_w) ([int]$first.source_h))
    } else {
        @()
    }
    $postUseReasons = @($postUseAccesses | ForEach-Object { $_.kind } | Sort-Object -Unique)
    $ineligibleReasons = @($directUnsafeForKey + $postUseReasons)
    if (-not $sourceValid) {
        $ineligibleReasons += 'invalid-source-rect'
    }

    [pscustomobject][ordered]@{
        tpage = $first.tpage
        tex_format = $first.tex_format
        page_x = $first.page_x
        page_y = $first.page_y
        page_w = $first.page_w
        page_h = $first.page_h
        clut = $first.clut
        clut_x = $first.clut_x
        clut_y = $first.clut_y
        uv_min_u = $first.uv_min_u
        uv_min_v = $first.uv_min_v
        uv_max_u = $first.uv_max_u
        uv_max_v = $first.uv_max_v
        source_x = $first.source_x
        source_y = $first.source_y
        source_w = $first.source_w
        source_h = $first.source_h
        primitive_count = $group.Count
        hazard_row_count = $hazardRows
        hazard_masks = ($hazardMasks -join '|')
        hazard_classes = ($hazardClasses -join '|')
        first_use_sequence = $firstUseSequence
        post_use_hazard_row_count = @($postUseAccesses).Count
        post_use_hazard_reasons = ($postUseReasons -join '|')
        eligible = if (@($ineligibleReasons).Count -eq 0) { 'true' } else { 'false' }
        ineligible_reasons = ($ineligibleReasons -join '|')
    }
}

$candidates |
    Sort-Object @{ Expression = { $_.eligible -eq 'true' }; Descending = $true },
                @{ Expression = { [int]$_.source_y }; Descending = $false },
                @{ Expression = { [int]$_.source_x }; Descending = $false },
                @{ Expression = { [int]$_.source_h }; Descending = $false },
                @{ Expression = { [int]$_.source_w }; Descending = $false },
                @{ Expression = { [int]$_.clut }; Descending = $false } |
    Export-Csv -LiteralPath $resolvedOutputPath -NoTypeInformation -Encoding utf8

$candidateCount = @($candidates).Count
$eligibleCount = @($candidates | Where-Object { $_.eligible -eq 'true' }).Count
Write-Host "[CTR Texture Trace] candidates=$candidateCount eligible=$eligibleCount output=$resolvedOutputPath"
