[CmdletBinding()]
param(
    [string]$SummarizerPath = (Join-Path $PSScriptRoot '..\tools\summarize-texture-trace.ps1')
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$resolvedSummarizerPath = (Resolve-Path -LiteralPath $SummarizerPath -ErrorAction Stop).Path
$testDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ('ctr-native-texture-trace-test-' + [Guid]::NewGuid().ToString('N'))
$tracePath = Join-Path $testDirectory 'fixture.csv'
$outputPath = Join-Path $testDirectory 'fixture.candidates.csv'

$fixture = @'
sequence,frame,event,hazard_mask,hazards,tpage,tex_format,page_x,page_y,page_w,page_h,clut,clut_x,clut_y,uv_min_u,uv_min_v,uv_max_u,uv_max_v,source_x,source_y,source_w,source_h,destination_x,destination_y,destination_w,destination_h
1,0,load-image,0x84,draw-page-write|load-image,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,0,0,64,64,64,0,64,64
2,1,texture-primitive,0x7,texture-read|clut-read|draw-page-write,1,4,64,0,64,256,64,0,1,0,0,15,15,64,0,4,16,0,0,320,240
3,1,texture-primitive,0x7,texture-read|clut-read|draw-page-write,2,4,128,0,64,256,128,0,2,0,0,15,15,128,0,4,16,0,0,320,240
4,2,move-image,0x24,draw-page-write|move-image,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,128,0,4,16,128,0,4,16
5,2,store-image,0x40,store-image,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,128,0,4,16,-1,-1,-1,-1
6,3,texture-primitive,0x19,texture-read|framebuffer-overlap|framebuffer-feedback,3,16,192,0,256,256,-1,-1,-1,0,0,31,31,192,0,32,32,0,0,320,240
'@

try {
    [System.IO.Directory]::CreateDirectory($testDirectory) | Out-Null
    [System.IO.File]::WriteAllText($tracePath, $fixture, [System.Text.UTF8Encoding]::new($false))

    & $resolvedSummarizerPath -TracePath $tracePath -OutputPath $outputPath
    $summary = @(Import-Csv -LiteralPath $outputPath)
    if ($summary.Count -ne 3) {
        throw "Expected three texture candidates, got $($summary.Count)."
    }

    $staticCandidate = @($summary | Where-Object { $_.tpage -eq '1' })
    if (($staticCandidate.Count -ne 1) -or ($staticCandidate[0].eligible -ne 'true') -or ($staticCandidate[0].first_use_sequence -ne '2')) {
        throw 'An initial load before first use or an ordinary destination draw incorrectly rejected the static candidate.'
    }

    $mutatedCandidate = @($summary | Where-Object { $_.tpage -eq '2' })
    if (($mutatedCandidate.Count -ne 1) -or ($mutatedCandidate[0].eligible -ne 'false') -or
        ($mutatedCandidate[0].post_use_hazard_reasons -notmatch 'post-use-write:move-image') -or
        ($mutatedCandidate[0].post_use_hazard_reasons -notmatch 'post-use-readback:store-image')) {
        throw 'A post-use overlapping mutation/readback did not reject its texture candidate.'
    }

    $feedbackCandidate = @($summary | Where-Object { $_.tpage -eq '3' })
    if (($feedbackCandidate.Count -ne 1) -or ($feedbackCandidate[0].eligible -ne 'false') -or
        ($feedbackCandidate[0].ineligible_reasons -notmatch 'framebuffer-feedback')) {
        throw 'A direct framebuffer-feedback texture primitive did not reject its candidate.'
    }

    Write-Host '[CTR Texture Trace Test] passed'
}
finally {
    if (Test-Path -LiteralPath $testDirectory) {
        [System.IO.Directory]::Delete($testDirectory, $true)
    }
}
