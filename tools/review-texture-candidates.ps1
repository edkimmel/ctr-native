[CmdletBinding(DefaultParameterSetName = 'Class')]
param(
    [Parameter(Mandatory = $true)]
    [ValidateNotNullOrEmpty()]
    [string]$CandidatesPath,

    [string]$ReviewOutputPath,

    [string]$ManifestOutputPath,

    # A single intentionally chosen class for every reviewed candidate.
    [Parameter(Mandatory = $true, ParameterSetName = 'Class')]
    [ValidateSet('font', 'ui-icon', 'ui-static', 'character-sprite')]
    [string]$AssetClass,

    # Use this when each candidate was classified in a separate reviewer CSV.
    [Parameter(Mandatory = $true, ParameterSetName = 'ClassColumn')]
    [ValidateNotNullOrEmpty()]
    [string]$AssetClassColumn,

    # Supplying a row-level approval column is the only way this tool emits
    # entries.  Omit it to create a header-only, deliberately inert draft.
    [string]$ApprovalColumn,

    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$requiredColumns = @(
    'eligible', 'tex_format', 'tpage', 'clut',
    'source_x', 'source_y', 'source_w', 'source_h'
)
$validAssetClasses = @('font', 'ui-icon', 'ui-static', 'character-sprite')
$validApprovalValues = @('approved', 'reviewed', 'true', 'yes', '1')
$validNonApprovalValues = @('', 'pending', 'false', 'no', '0')

function Get-StrictUnsigned {
    param(
        [object]$Value,
        [string]$Column,
        [string]$RowLabel,
        [int]$Maximum
    )

    $text = [string]$Value
    if ($text -notmatch '^[0-9]+$') {
        throw "Candidate $RowLabel has invalid $Column '$text'; expected an unsigned decimal integer."
    }
    try {
        $number = [Convert]::ToUInt32($text, [Globalization.CultureInfo]::InvariantCulture)
    }
    catch {
        throw "Candidate $RowLabel has invalid $Column '$text'; expected an unsigned decimal integer."
    }
    if ($number -gt $Maximum) {
        throw "Candidate $RowLabel has out-of-range $Column '$text'; maximum is $Maximum."
    }
    return [int]$number
}

function Test-ApprovedValue {
    param([object]$Value, [string]$Column, [string]$RowLabel)
    $normalized = ([string]$Value).Trim().ToLowerInvariant()
    if ($validApprovalValues -contains $normalized) {
        return $true
    }
    if ($validNonApprovalValues -contains $normalized) {
        return $false
    }
    throw "Candidate $RowLabel has invalid $Column flag '$Value'; expected approved/reviewed/true/yes/1 or pending/false/no/0."
}

function ConvertTo-MarkdownCell {
    param([object]$Value)
    return ([string]$Value).Replace('|', '\|').Replace("`r", ' ').Replace("`n", ' ')
}

function Get-DraftAssetPath {
    param(
        [string]$Class,
        [int]$Mode,
        [int]$Tpage,
        [int]$Clut,
        [int]$X,
        [int]$Y,
        [int]$Width,
        [int]$Height
    )

    # This names a future pack-relative CTRH asset only.  The review tool does
    # not create it, nor claim that the registry can load the draft yet.
    return ('textures/{0}/mode{1}-tpage{2}-clut{3}-x{4}-y{5}-w{6}-h{7}.ctrh' -f
        $Class, $Mode, $Tpage, $Clut, $X, $Y, $Width, $Height)
}

$resolvedCandidatesPath = (Resolve-Path -LiteralPath $CandidatesPath -ErrorAction Stop).Path
if (-not (Test-Path -LiteralPath $resolvedCandidatesPath -PathType Leaf)) {
    throw "Texture candidates must be a file: $resolvedCandidatesPath"
}

$header = Get-Content -LiteralPath $resolvedCandidatesPath -TotalCount 1
if ([string]::IsNullOrWhiteSpace($header)) {
    throw "Texture candidates CSV is empty: $resolvedCandidatesPath"
}
$headerColumns = @($header.Split(','))
foreach ($column in $requiredColumns) {
    if ($headerColumns -notcontains $column) {
        throw "Texture candidates CSV is missing required column '$column': $resolvedCandidatesPath"
    }
}
if ($PSCmdlet.ParameterSetName -eq 'ClassColumn' -and $headerColumns -notcontains $AssetClassColumn) {
    throw "Texture candidates CSV is missing asset-class column '$AssetClassColumn': $resolvedCandidatesPath"
}
if (-not [string]::IsNullOrWhiteSpace($ApprovalColumn) -and $headerColumns -notcontains $ApprovalColumn) {
    throw "Texture candidates CSV is missing approval column '$ApprovalColumn': $resolvedCandidatesPath"
}

$inputDirectory = Split-Path -Parent $resolvedCandidatesPath
$inputBaseName = [IO.Path]::GetFileNameWithoutExtension($resolvedCandidatesPath)
if ([string]::IsNullOrWhiteSpace($ReviewOutputPath)) {
    $ReviewOutputPath = Join-Path $inputDirectory ($inputBaseName + '.review.md')
}
if ([string]::IsNullOrWhiteSpace($ManifestOutputPath)) {
    $ManifestOutputPath = Join-Path $inputDirectory ($inputBaseName + '.presentation-manifest.draft')
}
$resolvedReviewOutputPath = [IO.Path]::GetFullPath($ReviewOutputPath)
$resolvedManifestOutputPath = [IO.Path]::GetFullPath($ManifestOutputPath)
if ($resolvedReviewOutputPath -eq $resolvedManifestOutputPath) {
    throw 'Review and manifest output paths must differ.'
}
foreach ($path in @($resolvedReviewOutputPath, $resolvedManifestOutputPath)) {
    if ((Test-Path -LiteralPath $path) -and (-not $Force)) {
        throw "Refusing to overwrite existing output without -Force: $path"
    }
}

$rows = @(Import-Csv -LiteralPath $resolvedCandidatesPath)
if ($rows.Count -eq 0) {
    throw "Texture candidates CSV has no rows: $resolvedCandidatesPath"
}

$reviewRows = [Collections.Generic.List[object]]::new()
$approvedEntries = [Collections.Generic.List[object]]::new()
$seenKeys = @{}
for ($index = 0; $index -lt $rows.Count; $index++) {
    $row = $rows[$index]
    $rowLabel = "row $($index + 2)"
    $class = if ($PSCmdlet.ParameterSetName -eq 'ClassColumn') { ([string]$row.$AssetClassColumn).Trim().ToLowerInvariant() } else { $AssetClass }
    if ($validAssetClasses -notcontains $class) {
        throw "Candidate $rowLabel has missing or unsupported asset class '$class'. Use font, ui-icon, ui-static, or character-sprite."
    }

    $mode = Get-StrictUnsigned $row.tex_format 'tex_format' $rowLabel 16
    if (@(4, 8, 16) -notcontains $mode) {
        throw "Candidate $rowLabel has unsupported tex_format '$mode'; expected 4, 8, or 16."
    }
    $tpage = Get-StrictUnsigned $row.tpage 'tpage' $rowLabel 2559
    $clut = Get-StrictUnsigned $row.clut 'clut' $rowLabel 65535
    $x = Get-StrictUnsigned $row.source_x 'source_x' $rowLabel 1023
    $y = Get-StrictUnsigned $row.source_y 'source_y' $rowLabel 511
    $width = Get-StrictUnsigned $row.source_w 'source_w' $rowLabel 1024
    $height = Get-StrictUnsigned $row.source_h 'source_h' $rowLabel 512
    if ($width -eq 0 -or $height -eq 0 -or ($x + $width) -gt 1024 -or ($y + $height) -gt 512) {
        throw "Candidate $rowLabel has an invalid source rectangle ($x,$y ${width}x$height)."
    }

    $eligibleText = ([string]$row.eligible).Trim().ToLowerInvariant()
    if (@('true', 'false') -notcontains $eligibleText) {
        throw "Candidate $rowLabel has invalid eligible value '$($row.eligible)'; expected true or false."
    }
    $eligible = $eligibleText -eq 'true'
    $approved = (-not [string]::IsNullOrWhiteSpace($ApprovalColumn)) -and (Test-ApprovedValue $row.$ApprovalColumn $ApprovalColumn $rowLabel)
    $key = "$mode`t$tpage`t$clut`t$x`t$y`t$width`t$height`t$class"
    $assetPath = Get-DraftAssetPath $class $mode $tpage $clut $x $y $width $height
    $status = if ($approved -and $eligible) { 'draft-entry' } elseif ($approved) { 'REJECTED: approved but ineligible' } elseif ($eligible) { 'awaiting-approval' } else { 'ineligible' }

    if ($approved -and -not $eligible) {
        throw "Candidate $rowLabel is approved but not eligible; refuse to draft a manifest entry. Reasons: $($row.ineligible_reasons)"
    }
    if ($approved) {
        if ($seenKeys.ContainsKey($key)) {
            throw "Candidate $rowLabel duplicates approved manifest key from $($seenKeys[$key])."
        }
        $seenKeys[$key] = $rowLabel
        [void]$approvedEntries.Add([pscustomobject]@{
            mode = $mode; tpage = $tpage; clut = $clut; x = $x; y = $y; width = $width; height = $height
            asset_class = $class; asset_path = $assetPath
        })
    }
    [void]$reviewRows.Add([pscustomobject]@{
        row = $index + 2; eligible = $eligible; approved = $approved; status = $status
        asset_class = $class; mode = $mode; tpage = $tpage; clut = $clut; x = $x; y = $y; width = $width; height = $height
        primitive_count = $row.primitive_count; hazard_classes = $row.hazard_classes; post_use_hazard_reasons = $row.post_use_hazard_reasons
        ineligible_reasons = $row.ineligible_reasons; asset_path = $assetPath
    })
}

if ($approvedEntries.Count -gt 64) {
    throw "Approved manifest has $($approvedEntries.Count) entries; registry limit is 64. Split the review into separate packs."
}

$manifestLines = [Collections.Generic.List[string]]::new()
[void]$manifestLines.Add("ctr-native-presentation-manifest`t1")
foreach ($entry in ($approvedEntries | Sort-Object mode, tpage, clut, y, x, height, width, asset_class)) {
    [void]$manifestLines.Add(("entry`t{0}`t{1}`t{2}`t{3}`t{4}`t{5}`t{6}`t{7}`t{8}" -f
        $entry.mode, $entry.tpage, $entry.clut, $entry.x, $entry.y, $entry.width, $entry.height, $entry.asset_class, $entry.asset_path))
}

$eligibleCount = @($reviewRows | Where-Object eligible).Count
$approvedCount = $approvedEntries.Count
$approvalMode = if ([string]::IsNullOrWhiteSpace($ApprovalColumn)) { 'none (header-only draft)' } else { "column '$ApprovalColumn'" }
$markdown = [Collections.Generic.List[string]]::new()
[void]$markdown.Add('# CTR Native Texture Candidate Review')
[void]$markdown.Add('')
[void]$markdown.Add("- Candidates: $($reviewRows.Count); trace-eligible: $eligibleCount; approved manifest entries: $approvedCount.")
[void]$markdown.Add("- Asset classification: $(if ($PSCmdlet.ParameterSetName -eq 'Class') { "fixed '$AssetClass'" } else { "column '$AssetClassColumn'" }).")
[void]$markdown.Add("- Approval gate: $approvalMode.")
[void]$markdown.Add('- The manifest is a draft only. This tool does not create assets, enable overrides, or modify the game.')
[void]$markdown.Add('')
[void]$markdown.Add('| CSV row | Status | Class | Exact source key | Proposed asset | Hazards / reason |')
[void]$markdown.Add('| ---: | --- | --- | --- | --- | --- |')
foreach ($entry in $reviewRows) {
    $source = "mode=$($entry.mode), tpage=$($entry.tpage), clut=$($entry.clut), rect=$($entry.x),$($entry.y) $($entry.width)x$($entry.height)"
    $hazards = @($entry.hazard_classes, $entry.post_use_hazard_reasons, $entry.ineligible_reasons) |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -Unique
    [void]$markdown.Add("| $($entry.row) | $(ConvertTo-MarkdownCell $entry.status) | $($entry.asset_class) | $(ConvertTo-MarkdownCell $source) | $(ConvertTo-MarkdownCell $entry.asset_path) | $(ConvertTo-MarkdownCell ($hazards -join '; ')) |")
}
[void]$markdown.Add('')
[void]$markdown.Add('## Draft-manifest gate')
[void]$markdown.Add('')
[void]$markdown.Add('Only rows that were trace-eligible and explicitly approved were emitted. A draft cannot be loaded until every named relative asset exists in a separately created pack.')

# Validate everything before either output is written; a bad review cannot leave
# a fresh partial manifest beside a stale report.
$utf8NoBom = [Text.UTF8Encoding]::new($false)
[IO.File]::WriteAllText($resolvedReviewOutputPath, ($markdown -join [Environment]::NewLine) + [Environment]::NewLine, $utf8NoBom)
[IO.File]::WriteAllText($resolvedManifestOutputPath, ($manifestLines -join [Environment]::NewLine) + [Environment]::NewLine, $utf8NoBom)
Write-Host "[CTR Texture Review] candidates=$($reviewRows.Count) eligible=$eligibleCount approved=$approvedCount review=$resolvedReviewOutputPath manifest=$resolvedManifestOutputPath"
