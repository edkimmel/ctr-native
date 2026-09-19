[CmdletBinding()]
param(
    # Use a Release build by default, but permit a different build directory
    # when a developer has an isolated native build.
    [string]$BuildDirectory,

    # This is primarily useful to automation tests and bespoke local builds.
    # When supplied it takes precedence over BuildDirectory.
    [string]$ExecutablePath,

    # An optional replay makes a trace capture repeatable. With no replay the
    # game starts normally and the operator chooses the menu/race coverage.
    [string]$ReplayPath,

    [ValidateSet('v1', 'v2')]
    [string]$ReplayFormat = 'v2',

    # Forward only discrete argv elements. Do not pass a shell command line:
    # the call operator below preserves spaces in replay and asset paths.
    [string[]]$GameArguments = @(),

    # A capture is always placed in a new child directory; this root itself is
    # never cleared or overwritten.
    [string]$OutputRoot
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function New-TextureTraceCaptureDirectory {
    param([Parameter(Mandatory = $true)][string]$Root)

    $resolvedRoot = [System.IO.Path]::GetFullPath($Root)
    [System.IO.Directory]::CreateDirectory($resolvedRoot) | Out-Null
    $dateDirectory = Join-Path $resolvedRoot (Get-Date -Format 'yyyyMMdd')
    [System.IO.Directory]::CreateDirectory($dateDirectory) | Out-Null

    # The random reservation component avoids reusing a pre-existing report
    # directory even when two captures begin in the same millisecond.
    $stamp = Get-Date -Format 'HHmmssfff'
    for ($attempt = 0; $attempt -lt 1000; $attempt++) {
        $reservationId = [Guid]::NewGuid().ToString('N')
        $candidate = Join-Path $dateDirectory "texture-trace-$stamp-$reservationId"
        try {
            if (-not (Test-Path -LiteralPath $candidate)) {
                [System.IO.Directory]::CreateDirectory($candidate) | Out-Null
                return [System.IO.Path]::GetFullPath($candidate)
            }
        }
        catch [System.IO.IOException] {
            continue
        }
    }

    throw "Could not reserve a unique texture-trace output directory below: $resolvedRoot"
}

function Restore-ProcessEnvironmentVariable {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [AllowNull()][string]$Value
    )

    [Environment]::SetEnvironmentVariable($Name, $Value, 'Process')
}

$sourceRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..') -ErrorAction Stop).Path
$BuildDirectory = if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    Join-Path $sourceRoot 'build-msvc-x86\Release'
}
else {
    $BuildDirectory
}
$OutputRoot = if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    Join-Path $sourceRoot 'debug\reports'
}
else {
    $OutputRoot
}
$summarizerPath = Join-Path $PSScriptRoot 'summarize-texture-trace.ps1'
if (-not (Test-Path -LiteralPath $summarizerPath -PathType Leaf)) {
    throw "Texture trace summarizer was not found: $summarizerPath"
}

if ([string]::IsNullOrWhiteSpace($ExecutablePath)) {
    $resolvedBuildDirectory = (Resolve-Path -LiteralPath $BuildDirectory -ErrorAction Stop).Path
    $executable = Join-Path $resolvedBuildDirectory 'ctr_native.exe'
}
else {
    $executable = [System.IO.Path]::GetFullPath($ExecutablePath)
}

if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "CTR Native executable was not found: $executable"
}

$arguments = [System.Collections.Generic.List[string]]::new()
if (-not [string]::IsNullOrWhiteSpace($ReplayPath)) {
    $resolvedReplayPath = (Resolve-Path -LiteralPath $ReplayPath -ErrorAction Stop).Path
    $replayArgument = if ($ReplayFormat -eq 'v2') { '--replay-v2' } else { '--replay' }
    [void]$arguments.Add($replayArgument)
    [void]$arguments.Add($resolvedReplayPath)
}
foreach ($argument in $GameArguments) {
    if ($null -eq $argument) {
        throw 'GameArguments cannot contain null values.'
    }
    [void]$arguments.Add($argument)
}

$captureDirectory = New-TextureTraceCaptureDirectory -Root $OutputRoot
$tracePath = Join-Path $captureDirectory 'texture-trace.csv'
$candidatePath = Join-Path $captureDirectory 'texture-trace.candidates.csv'
$metadataPath = Join-Path $captureDirectory 'capture.txt'

# Keep the process-local environment exactly as it was, including a missing
# value. The game inherits these only during this one capture invocation.
$previousTraceEnabled = [Environment]::GetEnvironmentVariable('CTR_NATIVE_TEXTURE_TRACE', 'Process')
$previousTracePath = [Environment]::GetEnvironmentVariable('CTR_NATIVE_TEXTURE_TRACE_PATH', 'Process')
$exitCode = $null

try {
    [Environment]::SetEnvironmentVariable('CTR_NATIVE_TEXTURE_TRACE', '1', 'Process')
    [Environment]::SetEnvironmentVariable('CTR_NATIVE_TEXTURE_TRACE_PATH', $tracePath, 'Process')

    Push-Location -LiteralPath $sourceRoot
    try {
        # The call operator receives an argv array, rather than a joined command
        # line, so replay paths and forwarded values retain their bounds.
        $gameArgumentArray = $arguments.ToArray()
        & $executable @gameArgumentArray
        $exitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
}
finally {
    Restore-ProcessEnvironmentVariable -Name 'CTR_NATIVE_TEXTURE_TRACE' -Value $previousTraceEnabled
    Restore-ProcessEnvironmentVariable -Name 'CTR_NATIVE_TEXTURE_TRACE_PATH' -Value $previousTracePath
}

$metadataLines = @(
    "capture_directory=$captureDirectory",
    "trace_path=$tracePath",
    "candidate_path=$candidatePath",
    "executable=$executable",
    "exit_code=$exitCode",
    "replay_path=$ReplayPath",
    "replay_format=$ReplayFormat",
    "arguments=$([string]::Join(' ', $arguments))"
)
[System.IO.File]::WriteAllLines($metadataPath, $metadataLines, [System.Text.UTF8Encoding]::new($false))

if ($exitCode -ne 0) {
    throw "CTR Native exited with code $exitCode. Trace capture retained at: $captureDirectory"
}
if (-not (Test-Path -LiteralPath $tracePath -PathType Leaf) -or (Get-Item -LiteralPath $tracePath).Length -eq 0) {
    throw "CTR Native produced no texture trace. Capture retained at: $captureDirectory"
}

try {
    & $summarizerPath -TracePath $tracePath -OutputPath $candidatePath
}
catch {
    throw "Texture trace summarization failed. Capture retained at: $captureDirectory. $($_.Exception.Message)"
}
if ($LASTEXITCODE -ne 0) {
    throw "Texture trace summarization failed. Capture retained at: $captureDirectory"
}
if (-not (Test-Path -LiteralPath $candidatePath -PathType Leaf)) {
    throw "Texture trace produced no candidate summary. Capture retained at: $captureDirectory"
}

$candidates = @(Import-Csv -LiteralPath $candidatePath)
if ($candidates.Count -eq 0) {
    throw "Texture trace produced no candidates. Capture retained at: $captureDirectory"
}
$eligibleCount = @($candidates | Where-Object { $_.eligible -eq 'true' }).Count
Write-Host "[CTR Texture Trace] capture=$captureDirectory candidates=$($candidates.Count) eligible=$eligibleCount"
