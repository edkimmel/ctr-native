[CmdletBinding()]
param(
    [string]$ReviewerPath = (Join-Path $PSScriptRoot '..\tools\review-texture-candidates.ps1')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$resolvedReviewerPath = (Resolve-Path -LiteralPath $ReviewerPath -ErrorAction Stop).Path
$testDirectory = Join-Path ([IO.Path]::GetTempPath()) ('ctr-native-texture-review-test-' + [Guid]::NewGuid().ToString('N'))
$candidatesPath = Join-Path $testDirectory 'fixture.candidates.csv'
$reviewPath = Join-Path $testDirectory 'fixture.review.md'
$manifestPath = Join-Path $testDirectory 'fixture.manifest'

$fixture = @'
eligible,tex_format,tpage,clut,source_x,source_y,source_w,source_h,primitive_count,hazard_classes,post_use_hazard_reasons,ineligible_reasons,approval,class
true,4,1,64,64,0,4,16,2,texture-read,,,approved,character-sprite
false,8,2,128,128,0,8,16,1,texture-read|framebuffer-feedback,,framebuffer-feedback,approved,ui-static
true,16,3,0,192,0,32,32,1,texture-read,,,pending,ui-icon
'@

function Assert-Throws {
    param([scriptblock]$Action, [string]$Message)
    try {
        & $Action
    }
    catch {
        return
    }
    throw $Message
}

try {
    [IO.Directory]::CreateDirectory($testDirectory) | Out-Null
    [IO.File]::WriteAllText($candidatesPath, $fixture, [Text.UTF8Encoding]::new($false))

    # Classification is mandatory, but an omitted approval column must create
    # a safe header-only manifest even when fixture rows say "approved".
    & $resolvedReviewerPath -CandidatesPath $candidatesPath -AssetClassColumn class -ReviewOutputPath $reviewPath -ManifestOutputPath $manifestPath
    $defaultManifest = @(Get-Content -LiteralPath $manifestPath)
    if ($defaultManifest.Count -ne 1 -or $defaultManifest[0] -ne "ctr-native-presentation-manifest`t1") {
        throw 'Unapproved default produced manifest entries.'
    }

    Remove-Item -LiteralPath $reviewPath, $manifestPath
    & $resolvedReviewerPath -CandidatesPath $candidatesPath -AssetClassColumn class -ApprovalColumn approval -ReviewOutputPath $reviewPath -ManifestOutputPath $manifestPath
    throw 'Approved ineligible row should have rejected the full review.'
}
catch {
    if ($_.Exception.Message -notmatch 'approved but not eligible') {
        throw
    }
}
finally {
    # The bad approval run is intentionally rejected, so start the successful
    # case with no output files and a fixture that leaves the unsafe row pending.
    Remove-Item -LiteralPath $reviewPath, $manifestPath -Force -ErrorAction SilentlyContinue
}

try {
    $safeFixture = $fixture.Replace('approved,ui-static', 'pending,ui-static')
    [IO.File]::WriteAllText($candidatesPath, $safeFixture, [Text.UTF8Encoding]::new($false))
    & $resolvedReviewerPath -CandidatesPath $candidatesPath -AssetClassColumn class -ApprovalColumn approval -ReviewOutputPath $reviewPath -ManifestOutputPath $manifestPath
    $manifest = @(Get-Content -LiteralPath $manifestPath)
    if ($manifest.Count -ne 2 -or $manifest[1] -ne "entry`t4`t1`t64`t64`t0`t4`t16`tcharacter-sprite`ttextures/character-sprite/mode4-tpage1-clut64-x64-y0-w4-h16.ctrh") {
        throw 'Approved exact eligible character sprite did not produce the strict expected manifest entry.'
    }
    $review = Get-Content -LiteralPath $reviewPath -Raw
    if ($review -notmatch 'awaiting-approval' -or $review -notmatch 'framebuffer-feedback') {
        throw 'Markdown review did not retain pending or rejected hazard information.'
    }

    $missingClassPath = Join-Path $testDirectory 'missing-class.csv'
    [IO.File]::WriteAllText($missingClassPath, $safeFixture.Replace(',character-sprite', ','), [Text.UTF8Encoding]::new($false))
    Assert-Throws { & $resolvedReviewerPath -CandidatesPath $missingClassPath -AssetClassColumn class -ApprovalColumn approval -ReviewOutputPath (Join-Path $testDirectory 'bad.md') -ManifestOutputPath (Join-Path $testDirectory 'bad.manifest') } 'Missing class was accepted.'

    Write-Host '[CTR Texture Review Test] passed'
}
finally {
    if (Test-Path -LiteralPath $testDirectory) {
        [IO.Directory]::Delete($testDirectory, $true)
    }
}
