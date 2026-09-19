[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$ReplayPath,

    [string]$BuildDirectory = (Join-Path $PSScriptRoot '..\build-msvc-x86\Release'),

    [int[]]$RenderScales = @(1, 2, 3, 4, 6, 8),

    [switch]$Fullscreen,

    # Optional root for per-scale --perf captures.  When provided, the runner
    # creates scale-1 through scale-8 subdirectories below this local path.
    [string]$PerfOutputDirectory
)

$allowedScales = @(1, 2, 3, 4, 6, 8)
$resolvedReplay = (Resolve-Path -LiteralPath $ReplayPath -ErrorAction Stop).Path
$resolvedBuildDirectory = (Resolve-Path -LiteralPath $BuildDirectory -ErrorAction Stop).Path
$executable = Join-Path $resolvedBuildDirectory 'ctr_native.exe'
$sourceRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$resolvedPerfOutputDirectory = $null

if (-not (Test-Path -LiteralPath $executable -PathType Leaf)) {
    throw "CTR Native executable was not found: $executable"
}

if (-not [string]::IsNullOrWhiteSpace($PerfOutputDirectory)) {
    $resolvedPerfOutputDirectory = [System.IO.Path]::GetFullPath($PerfOutputDirectory)
    [System.IO.Directory]::CreateDirectory($resolvedPerfOutputDirectory) | Out-Null
}

foreach ($renderScale in $RenderScales) {
    if ($allowedScales -notcontains $renderScale) {
        throw "Unsupported render scale $renderScale. Supported values: $($allowedScales -join ', ')."
    }
}

if (-not [BitConverter]::IsLittleEndian) {
    throw 'The render-scale replay sweep currently requires a little-endian host.'
}

# A release sweep requires CRV2: this is the first replay format that compares
# frozen input, VBlank packet counts, frame observations, and canonical-state
# domain digests.  V1 reports do not prove render-only determinism.
$replayHeaderBytes = 140
$replayFrameBytes = 616
$replayHeader = [byte[]]::new(24)
$stream = [System.IO.File]::Open($resolvedReplay, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
try {
    $offset = 0
    while ($offset -lt $replayHeader.Length) {
        $read = $stream.Read($replayHeader, $offset, $replayHeader.Length - $offset)
        if ($read -eq 0) {
            throw "Replay is too short to contain a CRV2 header: $resolvedReplay"
        }
        $offset += $read
    }
}
finally {
    $stream.Dispose()
}

$magic = [BitConverter]::ToUInt32($replayHeader, 0)
$formatVersion = [BitConverter]::ToUInt32($replayHeader, 4)
$headerBytes = [BitConverter]::ToUInt32($replayHeader, 8)
$frameBytes = [BitConverter]::ToUInt32($replayHeader, 12)
$flags = [BitConverter]::ToUInt32($replayHeader, 16)
$frameCount = [BitConverter]::ToUInt32($replayHeader, 20)
$expectedReplayBytes = [uint64]$replayHeaderBytes + ([uint64]$replayFrameBytes * [uint64]$frameCount)
$actualReplayBytes = [uint64](Get-Item -LiteralPath $resolvedReplay -ErrorAction Stop).Length

if (($magic -ne 0x32565243) -or ($formatVersion -ne 2) -or ($headerBytes -ne $replayHeaderBytes) -or
    ($frameBytes -ne $replayFrameBytes) -or (($flags -band 1) -eq 0) -or ($frameCount -eq 0) -or
    ($actualReplayBytes -ne $expectedReplayBytes)) {
    throw "Replay is not a finalized, complete CRV2 deterministic replay: $resolvedReplay"
}

$results = foreach ($renderScale in $RenderScales) {
    $arguments = @('--render-scale', "$renderScale", '--replay-v2', $resolvedReplay)
    if ($Fullscreen) {
        $arguments += '--fullscreen'
    }
    $scalePerfDirectory = $null
    if ($null -ne $resolvedPerfOutputDirectory) {
        $scalePerfDirectory = Join-Path $resolvedPerfOutputDirectory "scale-$renderScale"
        [System.IO.Directory]::CreateDirectory($scalePerfDirectory) | Out-Null
        $arguments += @('--perf', '--perf-dir', $scalePerfDirectory)
    }

    $startedAt = Get-Date
    Push-Location -LiteralPath $sourceRoot
    try {
        # The call operator preserves the replay path as one argv element.
        # Start-Process joins its argument array, which can lose path quoting.
        & $executable @arguments
        $exitCode = $LASTEXITCODE
    }
    finally {
        Pop-Location
    }
    $finishedAt = Get-Date

    [pscustomobject]@{
        RenderScale = $renderScale
        Fullscreen = [bool]$Fullscreen
        PerfDirectory = $scalePerfDirectory
        ExitCode = $exitCode
        Passed = ($exitCode -eq 0)
        DurationSeconds = [math]::Round(($finishedAt - $startedAt).TotalSeconds, 3)
    }
}

$results | Format-Table -AutoSize

if (@($results | Where-Object { -not $_.Passed }).Count -ne 0) {
    exit 1
}
