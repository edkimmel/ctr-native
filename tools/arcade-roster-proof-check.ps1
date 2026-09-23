[CmdletBinding()]
param(
    # Internal ctr_native build that accepts --arcade-roster-proof.
    [Parameter(Mandatory = $true)]
    [string]$Executable,

    # Absolute directory for the four reports and their stdout/stderr logs.
    # Each run uses it as its working directory; the game itself still writes
    # the gitignored `Crash Team Racing.log` in the repository root.
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    # User-supplied disc image.  The check skips (exit 77) when it is absent.
    # Defaults to <repo>\assets\ctr-u.bin, resolved below: Windows PowerShell
    # 5.1 leaves $PSScriptRoot empty inside the param defaults of a
    # [CmdletBinding()] script run with -File.
    [string]$AssetsFile,

    # Race ticks each run logs (--arcade-roster-proof-ticks).
    [int]$Ticks = 900,

    # Seconds each run may take (sequential), or all four together
    # (-Parallel).
    [int]$TimeoutSeconds = 600,

    # Run the four proofs at once instead of one after another.  Faster, but
    # not the default: see the note on host timing below.
    [switch]$Parallel
)

# Live roster determinism check (docs/ROSTER_MILESTONE.md section 3.4, R-6).
# Runs four live roster proofs and compares their reports:
#   A  seed 0x5EED, dwell 0     (launches from the title)
#   B  seed 0x5EED, dwell 0     (A again: byte-identical report)
#   C  seed 0x5EED, dwell 5400  (launches from inside the attract demo race:
#                                same RNG, input, and drivers digests as A at
#                                every race tick; control may differ, RS-12)
#   D  seed 0x5EEE, dwell 0     (another seed: a different config digest, bank
#                                digest, and race tick 0 RNG digest)
# The runs are started one after another by default.  The report's control
# digest includes gGT->frameTimer_VsyncCallback, the count of emitted
# VBlanks since boot, and the native VBlank pacer faithfully emits the late
# VBlanks of a frame that overran its slot (platform/native_platform.c,
# Native_CatchUpDueVBlanks): one host hitch before the race moves that count,
# and so every later control digest, while the race itself is unchanged.
# Four parallel Debug runs under ctest produced exactly that once (A's
# frameTimer one VBlank ahead of B's, all else identical), so the runs are
# kept off each other's timing; -Parallel still runs them at once.
#
# Exit codes: 0 pass, 1 fail, 77 skipped (ctest SKIP_RETURN_CODE).
$skipExitCode = 77
$noDisplayMarker = 'No displays available'
$notInternalMarker = '--arcade-roster-proof is available in internal builds only.'
$tickPattern = '^tick ([0-9]+) control ([0-9a-f]{16}) rng ([0-9a-f]{16}) input ([0-9a-f]{16}) drivers ([0-9a-f]{64})$'

function Exit-Skipped([string]$Reason) {
    Write-Output "SKIPPED: $Reason"
    exit $skipExitCode
}

function Exit-Failed([string]$Reason) {
    Write-Output "FAIL: $Reason"
    exit 1
}

# Start-Process joins its argument array with spaces, so quote any element
# that needs it.
function ConvertTo-ProcessArgument([string]$Value) {
    if ($Value -notmatch '[\s"]') {
        return $Value
    }
    return '"' + $Value.TrimEnd('\') + '"'
}

# Reads both logs, sharing them: the redirection may still hold them open
# for a moment after the process exited.
function Get-RunLog($Run) {
    $text = ''
    foreach ($path in @($Run.StdoutPath, $Run.StderrPath)) {
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            $share = [System.IO.FileShare]([int][System.IO.FileShare]::ReadWrite -bor [int][System.IO.FileShare]::Delete)
            $stream = [System.IO.File]::Open($path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, $share)
            try {
                $reader = New-Object System.IO.StreamReader($stream)
                $text += $reader.ReadToEnd()
                $reader.Dispose()
            }
            finally {
                $stream.Dispose()
            }
            $text += "`n"
        }
    }
    return $text
}

function Stop-StartedRuns($Runs) {
    foreach ($run in $Runs) {
        if (($null -ne $run.Process) -and (-not $run.Process.HasExited)) {
            try {
                $run.Process.Kill()
                $run.Process.WaitForExit(10000) | Out-Null
            }
            catch {
                # The process exited between the check and the kill.
            }
        }
    }
}

function Start-Run($Run) {
    $arguments = @('--arcade-roster-proof', $Run.ReportPath, '--arcade-roster-proof-seed', $Run.Seed,
        '--arcade-roster-proof-dwell', "$($Run.Dwell)", '--arcade-roster-proof-ticks', "$Ticks")
    $argumentLine = ($arguments | ForEach-Object { ConvertTo-ProcessArgument $_ }) -join ' '
    $process = Start-Process -FilePath $resolvedExecutable -ArgumentList $argumentLine `
        -WorkingDirectory $resolvedOutput -NoNewWindow -PassThru `
        -RedirectStandardOutput $Run.StdoutPath -RedirectStandardError $Run.StderrPath
    # Touching Handle keeps ExitCode readable after the process exits.
    $null = $process.Handle
    $Run.Process = $process
    $Run.StartedAt = Get-Date
}

# Waits for the given runs.  A missing display or a non-internal build is
# known as soon as one run exits, so the rest are stopped early.
function Wait-Runs($Runs, $AllRuns) {
    while ($true) {
        $pending = @($Runs | Where-Object { -not $_.Process.HasExited })
        foreach ($run in @($Runs | Where-Object { $_.Process.HasExited })) {
            $log = Get-RunLog $run
            $reason = $null
            if ($log.Contains($noDisplayMarker)) {
                $reason = "no display available to SDL (run $($run.Name), see $($run.StderrPath)); a desktop session is required"
            }
            elseif ($log.Contains($notInternalMarker)) {
                $reason = "--arcade-roster-proof was rejected: the executable is not an internal (CTR_INTERNAL) build (run $($run.Name), see $($run.StderrPath))"
            }
            if ($null -ne $reason) {
                Stop-StartedRuns $AllRuns
                Exit-Skipped $reason
            }
        }
        if ($pending.Count -eq 0) {
            break
        }
        if ((Get-Date) -gt $deadline) {
            Stop-StartedRuns $AllRuns
            Exit-Failed "timed out after $TimeoutSeconds s waiting for run(s) $(($pending | ForEach-Object { $_.Name }) -join ', ') (logs in $resolvedOutput)"
        }
        Start-Sleep -Milliseconds 500
    }
    foreach ($run in $Runs) {
        $run.Process.WaitForExit()
    }
}

# Parses a report: the header lines by key, the slot lines, and the tick lines.
function Read-Report($Run) {
    $lines = @([System.IO.File]::ReadAllLines($Run.ReportPath))
    $report = [pscustomobject]@{
        Lines = $lines
        Header = @{}
        Slots = @()
        Seeded = $null
        Ticks = @()
        End = $null
        Problems = @()
    }
    foreach ($line in $lines) {
        if ($line -match $tickPattern) {
            $report.Ticks += [pscustomobject]@{
                Tick = [int]$Matches[1]; Control = $Matches[2]; Rng = $Matches[3]; Input = $Matches[4]; Drivers = $Matches[5]; Line = $line
            }
        }
        elseif ($line -match '^slot [0-9]+ ') {
            $report.Slots += $line
        }
        elseif ($line -match '^seeded ') {
            $report.Seeded = $line
        }
        elseif ($line -match '^end ticks ([0-9]+)$') {
            $report.End = [int]$Matches[1]
        }
        elseif ($line -match '^(result|launch window|config digest|race plan digest|bot setup plan digest|bank digest) (.*)$') {
            $report.Header[$Matches[1]] = $Matches[2]
        }
    }
    if (($lines.Count -lt 2) -or ($lines[0] -ne 'arcade roster proof v4') -or ($lines[1] -ne 'drivers digest excludes physics')) {
        $report.Problems += 'the report does not start with the v4 header and "drivers digest excludes physics"'
    }
    if ($report.Header['result'] -ne 'PASS (0)') {
        $report.Problems += "result is '$($report.Header['result'])', not 'PASS (0)'"
    }
    if ($report.Ticks.Count -ne $Ticks) {
        $report.Problems += "$($report.Ticks.Count) tick lines, expected $Ticks"
    }
    for ($i = 0; $i -lt $report.Ticks.Count; $i++) {
        if ($report.Ticks[$i].Tick -ne $i) {
            $report.Problems += "tick line $i is numbered $($report.Ticks[$i].Tick)"
            break
        }
    }
    if (($lines.Count -eq 0) -or ($lines[$lines.Count - 1] -ne "end ticks $Ticks")) {
        $report.Problems += "the report does not end with 'end ticks $Ticks'"
    }
    if (($null -eq $report.Seeded) -or (-not $report.Seeded.EndsWith(' match 1'))) {
        $report.Problems += "the seeded line is missing or does not match: '$($report.Seeded)'"
    }
    if ($report.Slots.Count -ne 8) {
        $report.Problems += "$($report.Slots.Count) slot lines, expected 8"
    }
    return $report
}

if ([string]::IsNullOrWhiteSpace($AssetsFile)) {
    $scriptDirectory = $PSScriptRoot
    if ([string]::IsNullOrWhiteSpace($scriptDirectory)) {
        $scriptDirectory = Split-Path -Parent $MyInvocation.MyCommand.Path
    }
    $AssetsFile = [System.IO.Path]::GetFullPath((Join-Path $scriptDirectory '..\assets\ctr-u.bin'))
}
if (-not (Test-Path -LiteralPath $AssetsFile -PathType Leaf)) {
    Exit-Skipped "assets file not found: $AssetsFile (user-supplied disc image required)"
}
if (-not [System.IO.Path]::IsPathRooted($OutputDirectory)) {
    Exit-Failed "OutputDirectory must be an absolute path: $OutputDirectory"
}
if (($Ticks -lt 1) -or ($Ticks -gt 3600)) {
    Exit-Failed "invalid tick count $Ticks (1..3600)"
}
$resolvedExecutable = (Resolve-Path -LiteralPath $Executable -ErrorAction Stop).Path
$resolvedOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
[System.IO.Directory]::CreateDirectory($resolvedOutput) | Out-Null

$specs = @(
    @{ Name = 'A'; Seed = '0x5EED'; Dwell = 0; Window = 'title' },
    @{ Name = 'B'; Seed = '0x5EED'; Dwell = 0; Window = 'title' },
    @{ Name = 'C'; Seed = '0x5EED'; Dwell = 5400; Window = 'demo race' },
    @{ Name = 'D'; Seed = '0x5EEE'; Dwell = 0; Window = 'title' })
$runs = @()
foreach ($spec in $specs) {
    $run = [pscustomobject]@{
        Name = $spec.Name
        Seed = $spec.Seed
        Dwell = $spec.Dwell
        Window = $spec.Window
        ReportPath = Join-Path $resolvedOutput "$($spec.Name).report.txt"
        StdoutPath = Join-Path $resolvedOutput "$($spec.Name).stdout.log"
        StderrPath = Join-Path $resolvedOutput "$($spec.Name).stderr.log"
        Process = $null
        StartedAt = $null
    }
    # A stale report from an earlier run must never pass for this one.
    foreach ($stale in @($run.ReportPath, $run.StdoutPath, $run.StderrPath)) {
        if (Test-Path -LiteralPath $stale) {
            Remove-Item -LiteralPath $stale -Force -ErrorAction Stop
        }
    }
    $runs += $run
}

$mode = 'sequential'
if ($Parallel) {
    $mode = 'parallel'
}
Write-Output "arcade roster determinism check: 4 runs ($mode), $Ticks race ticks each"
Write-Output "executable: $resolvedExecutable"
Write-Output "output:     $resolvedOutput"

if ($Parallel) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    foreach ($run in $runs) {
        Start-Run $run
    }
    Wait-Runs $runs $runs
}
else {
    foreach ($run in $runs) {
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        Start-Run $run
        Wait-Runs @($run) $runs
    }
}

$failures = @()
foreach ($run in $runs) {
    $exitCode = $run.Process.ExitCode
    $seconds = [math]::Round(($run.Process.ExitTime - $run.StartedAt).TotalSeconds, 1)
    Write-Output ("run {0} seed {1} dwell {2,-4} exit {3} in {4} s" -f $run.Name, $run.Seed, $run.Dwell, $exitCode, $seconds)
    if ($exitCode -ne 0) {
        $failures += "run $($run.Name) exited $exitCode; see $($run.StdoutPath) and $($run.StderrPath)"
    }
    elseif (-not (Test-Path -LiteralPath $run.ReportPath -PathType Leaf)) {
        $failures += "run $($run.Name) exited 0 but wrote no report $($run.ReportPath)"
    }
}
if ($failures.Count -ne 0) {
    foreach ($failure in $failures) {
        Write-Output "FAIL: $failure"
    }
    Write-Output "arcade roster determinism check: FAIL"
    exit 1
}

$reports = @{}
foreach ($run in $runs) {
    $report = Read-Report $run
    foreach ($problem in $report.Problems) {
        $failures += "report $($run.Name): $problem"
    }
    if ($report.Header['launch window'] -ne $run.Window) {
        $failures += "report $($run.Name): launch window '$($report.Header['launch window'])', expected '$($run.Window)'"
    }
    $reports[$run.Name] = $report
}
if ($failures.Count -ne 0) {
    foreach ($failure in $failures) {
        Write-Output "FAIL: $failure"
    }
    Write-Output "arcade roster determinism check: FAIL"
    exit 1
}
$a = $reports['A']
$b = $reports['B']
$c = $reports['C']
$d = $reports['D']

# A and B: byte-identical.
$bytesA = [System.IO.File]::ReadAllBytes($runs[0].ReportPath)
$bytesB = [System.IO.File]::ReadAllBytes($runs[1].ReportPath)
$identical = $bytesA.Length -eq $bytesB.Length
for ($i = 0; $identical -and ($i -lt $bytesA.Length); $i++) {
    if ($bytesA[$i] -ne $bytesB[$i]) {
        $identical = $false
    }
}
if ($identical) {
    Write-Output "A = B: byte-identical ($($bytesA.Length) bytes)"
}
else {
    $firstLine = $null
    for ($i = 0; $i -lt [Math]::Min($a.Lines.Count, $b.Lines.Count); $i++) {
        if ($a.Lines[$i] -ne $b.Lines[$i]) {
            $firstLine = "line $($i + 1): A '$($a.Lines[$i])' B '$($b.Lines[$i])'"
            break
        }
    }
    $failures += "A and B reports differ (first difference $firstLine)"
}

# C against A: the setup evidence and every tick's rng, input, and drivers.
foreach ($key in @('config digest', 'race plan digest', 'bot setup plan digest', 'bank digest')) {
    if ($c.Header[$key] -ne $a.Header[$key]) {
        $failures += "C differs from A in the $key (A $($a.Header[$key]), C $($c.Header[$key]))"
    }
}
if ($c.Seeded -ne $a.Seeded) {
    $failures += "C differs from A in the seeded line (A '$($a.Seeded)', C '$($c.Seeded)')"
}
for ($i = 0; $i -lt 8; $i++) {
    if ($c.Slots[$i] -ne $a.Slots[$i]) {
        $failures += "C differs from A in slot line $i (A '$($a.Slots[$i])', C '$($c.Slots[$i])')"
    }
}
$controlDifferences = 0
$firstControl = $null
$firstSimulation = $null
for ($i = 0; $i -lt $Ticks; $i++) {
    $tickA = $a.Ticks[$i]
    $tickC = $c.Ticks[$i]
    if ($tickA.Control -ne $tickC.Control) {
        $controlDifferences++
        if ($null -eq $firstControl) {
            $firstControl = $i
        }
    }
    foreach ($domain in @('Rng', 'Input', 'Drivers')) {
        if (($tickA.$domain -ne $tickC.$domain) -and ($null -eq $firstSimulation)) {
            $firstSimulation = "tick $i $($domain.ToLowerInvariant()) (A $($tickA.$domain), C $($tickC.$domain))"
        }
    }
}
if ($null -ne $firstSimulation) {
    $failures += "C differs from A at $firstSimulation"
}
else {
    Write-Output "C = A: config, race plan, bot setup, and bank digests, the seeded line, 8 slot lines, and the rng, input, and drivers digests of all $Ticks ticks"
}
if ($controlDifferences -eq 0) {
    Write-Output "C vs A control digests (informational, RS-12): identical at all $Ticks ticks"
}
else {
    Write-Output "C vs A control digests (informational, RS-12): $controlDifferences of $Ticks ticks differ (first at tick $firstControl); boot-relative counters"
}

# D against A: another seed changes the config, the bank, and race tick 0's RNG.
foreach ($key in @('config digest', 'bank digest')) {
    if ($d.Header[$key] -eq $a.Header[$key]) {
        $failures += "D does not differ from A in the $key ($($a.Header[$key]))"
    }
}
if ($d.Ticks[0].Rng -eq $a.Ticks[0].Rng) {
    $failures += "D does not differ from A in the tick 0 rng digest ($($a.Ticks[0].Rng))"
}
else {
    Write-Output "D != A: config digest, bank digest, and tick 0 rng digest (A $($a.Ticks[0].Rng), D $($d.Ticks[0].Rng))"
}

Write-Output "report A first tick: $($a.Ticks[0].Line)"
Write-Output "report A last tick:  $($a.Ticks[$Ticks - 1].Line)"
Write-Output ''
if ($failures.Count -ne 0) {
    foreach ($failure in $failures) {
        Write-Output "FAIL: $failure"
    }
    Write-Output "arcade roster determinism check: FAIL ($($failures.Count) failure(s); reports in $resolvedOutput)"
    exit 1
}
Write-Output "arcade roster determinism check: PASS"
exit 0
