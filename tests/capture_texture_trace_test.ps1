[CmdletBinding()]
param([string]$CapturePath)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$CapturePath = if ([string]::IsNullOrWhiteSpace($CapturePath)) {
    Join-Path $PSScriptRoot '..\tools\capture-texture-trace.ps1'
}
else {
    $CapturePath
}
$resolvedCapturePath = (Resolve-Path -LiteralPath $CapturePath -ErrorAction Stop).Path
$testDirectory = Join-Path ([System.IO.Path]::GetTempPath()) ('ctr-native-capture-texture-trace-test-' + [Guid]::NewGuid().ToString('N'))
$outputRoot = Join-Path $testDirectory 'reports'
$writerPath = Join-Path $testDirectory 'write-trace.ps1'
$emptyWriterPath = Join-Path $testDirectory 'write-nothing.ps1'
$headerOnlyWriterPath = Join-Path $testDirectory 'write-header-only.ps1'
$replayDirectory = Join-Path $testDirectory 'replay with spaces'
$replayPath = Join-Path $replayDirectory 'input.v2.ctrreplay'
$previousTraceEnabled = [Environment]::GetEnvironmentVariable('CTR_NATIVE_TEXTURE_TRACE', 'Process')
$previousTracePath = [Environment]::GetEnvironmentVariable('CTR_NATIVE_TEXTURE_TRACE_PATH', 'Process')

$writer = @'
param([Parameter(ValueFromRemainingArguments = $true)][string[]]$Forwarded)
$csv = @"
sequence,frame,event,hazard_mask,hazards,tpage,tex_format,page_x,page_y,page_w,page_h,clut,clut_x,clut_y,uv_min_u,uv_min_v,uv_max_u,uv_max_v,source_x,source_y,source_w,source_h,destination_x,destination_y,destination_w,destination_h
1,0,texture-primitive,0x7,texture-read|clut-read|draw-page-write,1,4,64,0,64,256,64,0,1,0,0,15,15,64,0,4,16,0,0,320,240
"@
[System.IO.File]::WriteAllText($env:CTR_NATIVE_TEXTURE_TRACE_PATH, $csv, [System.Text.UTF8Encoding]::new($false))
[System.IO.File]::WriteAllText((Join-Path (Split-Path -Parent $env:CTR_NATIVE_TEXTURE_TRACE_PATH) 'argv.txt'), ($Forwarded -join "`n"), [System.Text.UTF8Encoding]::new($false))
exit 0
'@

$headerOnlyWriter = @'
$header = 'sequence,frame,event,hazard_mask,hazards,tpage,tex_format,page_x,page_y,page_w,page_h,clut,clut_x,clut_y,uv_min_u,uv_min_v,uv_max_u,uv_max_v,source_x,source_y,source_w,source_h,destination_x,destination_y,destination_w,destination_h'
[System.IO.File]::WriteAllText($env:CTR_NATIVE_TEXTURE_TRACE_PATH, $header, [System.Text.UTF8Encoding]::new($false))
exit 0
'@

try {
    [System.IO.Directory]::CreateDirectory($testDirectory) | Out-Null
    [System.IO.Directory]::CreateDirectory($replayDirectory) | Out-Null
    [System.IO.File]::WriteAllText($replayPath, 'synthetic replay', [System.Text.UTF8Encoding]::new($false))
    [System.IO.File]::WriteAllText($writerPath, $writer, [System.Text.UTF8Encoding]::new($false))
    [System.IO.File]::WriteAllText($emptyWriterPath, 'exit 0', [System.Text.UTF8Encoding]::new($false))
    [System.IO.File]::WriteAllText($headerOnlyWriterPath, $headerOnlyWriter, [System.Text.UTF8Encoding]::new($false))

    [Environment]::SetEnvironmentVariable('CTR_NATIVE_TEXTURE_TRACE', 'caller-value', 'Process')
    [Environment]::SetEnvironmentVariable('CTR_NATIVE_TEXTURE_TRACE_PATH', 'caller-path', 'Process')
    & $resolvedCapturePath -ExecutablePath $writerPath -OutputRoot $outputRoot -ReplayPath $replayPath -GameArguments @('--tag', 'value with spaces')
    if ($LASTEXITCODE -ne 0) {
        throw "Capture wrapper returned $LASTEXITCODE for synthetic trace."
    }

    & $resolvedCapturePath -ExecutablePath $writerPath -OutputRoot $outputRoot -ReplayPath $replayPath -GameArguments @('--tag', 'value with spaces')
    $captures = @(Get-ChildItem -LiteralPath $outputRoot -Directory -Recurse | Where-Object { $_.Name -like 'texture-trace-*' })
    if ($captures.Count -ne 2) {
        throw "Expected two unique capture directories, got $($captures.Count)."
    }
    foreach ($capture in $captures) {
        $captureDirectory = $capture.FullName
        if (-not (Test-Path -LiteralPath (Join-Path $captureDirectory 'texture-trace.csv') -PathType Leaf) -or
            -not (Test-Path -LiteralPath (Join-Path $captureDirectory 'texture-trace.candidates.csv') -PathType Leaf)) {
            throw 'Capture wrapper did not retain trace and candidate files.'
        }
        $argv = @(Get-Content -LiteralPath (Join-Path $captureDirectory 'argv.txt'))
        if (($argv.Count -ne 4) -or ($argv[0] -ne '--replay-v2') -or
            ($argv[1] -ne [System.IO.Path]::GetFullPath($replayPath)) -or
            ($argv[2] -ne '--tag') -or ($argv[3] -ne 'value with spaces')) {
            throw 'Capture wrapper did not preserve replay and forwarded argv elements.'
        }
    }
    if (([Environment]::GetEnvironmentVariable('CTR_NATIVE_TEXTURE_TRACE', 'Process') -ne 'caller-value') -or
        ([Environment]::GetEnvironmentVariable('CTR_NATIVE_TEXTURE_TRACE_PATH', 'Process') -ne 'caller-path')) {
        throw 'Capture wrapper did not restore caller texture-trace environment variables.'
    }

    $emptyFailed = $false
    try {
        & $resolvedCapturePath -ExecutablePath $emptyWriterPath -OutputRoot $outputRoot
    }
    catch {
        $emptyFailed = $_.Exception.Message -match 'produced no texture trace'
    }
    if (-not $emptyFailed) {
        throw 'Capture wrapper accepted a run that produced no trace.'
    }

    $noCandidateFailed = $false
    try {
        & $resolvedCapturePath -ExecutablePath $headerOnlyWriterPath -OutputRoot $outputRoot
    }
    catch {
        $noCandidateFailed = $_.Exception.Message -match 'summarization failed'
    }
    if (-not $noCandidateFailed) {
        throw 'Capture wrapper accepted a trace that produced no candidates.'
    }
    if (([Environment]::GetEnvironmentVariable('CTR_NATIVE_TEXTURE_TRACE', 'Process') -ne 'caller-value') -or
        ([Environment]::GetEnvironmentVariable('CTR_NATIVE_TEXTURE_TRACE_PATH', 'Process') -ne 'caller-path')) {
        throw 'Capture wrapper did not restore caller environment after a failed capture.'
    }

    Write-Host '[CTR Texture Trace Capture Test] passed'
}
finally {
    [Environment]::SetEnvironmentVariable('CTR_NATIVE_TEXTURE_TRACE', $previousTraceEnabled, 'Process')
    [Environment]::SetEnvironmentVariable('CTR_NATIVE_TEXTURE_TRACE_PATH', $previousTracePath, 'Process')
    if (Test-Path -LiteralPath $testDirectory) {
        [System.IO.Directory]::Delete($testDirectory, $true)
    }
}
