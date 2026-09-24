[CmdletBinding()]
param(
    # Internal ctr_native build that accepts --arcade-link-autopilot.
    [Parameter(Mandatory = $true)]
    [string]$Executable,

    # Absolute directory for the two reports and their stdout/stderr logs.
    # Both runs use it as their working directory; the game itself still
    # writes the gitignored `Crash Team Racing.log` in its base directory.
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    # User-supplied disc image.  The check skips (exit 77) when it is absent.
    # Defaults to <repo>\assets\ctr-u.bin, resolved below: Windows PowerShell
    # 5.1 leaves $PSScriptRoot empty inside the param defaults of a
    # [CmdletBinding()] script run with -File.
    [string]$AssetsFile,

    # Seconds both runs together may take.
    [int]$TimeoutSeconds = 600
)

# Two-process live launch gate (docs/RACE_LAUNCH_MILESTONE.md section 4
# RL-15, slice RL-S10).  Starts two internal ctr_native processes at once,
# linked over loopback:
#   cab1  --arcade-link cab1 --arcade-link-port 7001 --arcade-link-peer 127.0.0.1:7002
#   cab2  --arcade-link cab2 --arcade-link-port 7002 --arcade-link-peer 127.0.0.1:7001
# each with --arcade-link-autopilot <report>, which drives the link host's own
# inputs (never a pad): START on the attract screen, CROSS on each select
# item, race 1, REMATCH, race 2, EXIT, then the exit with the result code
# (include/platform/native_arcade_link_autopilot.h).  It requires:
#   - both processes exit 0;
#   - each report is "arcade link autopilot v1" for its cabinet with
#     "result PASS (0)" and two races, each with its agreed-match line and
#     its validated line (the RL-12 config, plan, bots, and bank digests),
#     ending "end races 2";
#   - the agreed-match line and the four digests of the k-th race are equal
#     across the two processes.  The k-th race of each report is compared,
#     never equal launch numbers: the race caller's launch number counts
#     launch attempts on its own cabinet and may differ between the two
#     (RL-S7 interpretation (d));
#   - race 2's config digest differs from race 1's (the rematch goes through
#     a new select);
#   - each report agrees with its own stdout: the k-th "arcade link: agreed
#     match" line and the k-th "arcade link: race <n> validated" line (with
#     n its report's launch number) carry the same text and digests.
#
# Skips (77) without the disc image, without a display, with a non-internal
# build (the option is rejected), or with an unknown build identity (a build
# from a dirty tree: the link refuses to start).  A skip is not a pass.
#
# The ports are fixed, so the ctest runs RUN_SERIAL.  Every ctr_native the
# check started is stopped when the check exits or is interrupted.
#
# Exit codes: 0 pass, 1 fail, 77 skipped (ctest SKIP_RETURN_CODE).
$skipExitCode = 77
$noDisplayMarker = 'No displays available'
$notInternalMarker = '--arcade-link-autopilot is available in internal builds only.'
$unknownIdentityMarker = 'arcade link requires a known build and content identity.'
$races = 2
$agreedPattern = '^race ([0-9]+) (agreed match track [0-9]+ laps [0-9]+ seed 0x[0-9A-F]{16} slots( [0-9]+){8} \([12B-]{8}\))$'
$validatedPattern = '^race ([0-9]+) validated launch ([0-9]+) config ([0-9a-f]{64}) plan ([0-9a-f]{64}) bots ([0-9a-f]{64}) bank ([0-9a-f]{64})$'
$stdoutAgreedPattern = '^\[CTR Native\] arcade link: (agreed match .*)$'
$stdoutValidatedPattern = '^\[CTR Native\] arcade link: race ([0-9]+) validated config ([0-9a-f]{64}) plan ([0-9a-f]{64}) bots ([0-9a-f]{64}) bank ([0-9a-f]{64})$'
$runs = @()

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

# Reads a log, sharing it: the redirection may still hold it open for a
# moment after the process exited.
function Read-SharedText([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return ''
    }
    $share = [System.IO.FileShare]([int][System.IO.FileShare]::ReadWrite -bor [int][System.IO.FileShare]::Delete)
    $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, $share)
    try {
        $reader = New-Object System.IO.StreamReader($stream)
        $text = $reader.ReadToEnd()
        $reader.Dispose()
    }
    finally {
        $stream.Dispose()
    }
    return $text
}

function Get-RunLog($Run) {
    return (Read-SharedText $Run.StdoutPath) + "`n" + (Read-SharedText $Run.StderrPath)
}

# Stops every run this check started that is still running.
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
    $arguments = @('--arcade-link', $Run.Cab, '--arcade-link-port', $Run.Port, '--arcade-link-peer', $Run.Peer,
        '--arcade-link-autopilot', $Run.ReportPath)
    $argumentLine = ($arguments | ForEach-Object { ConvertTo-ProcessArgument $_ }) -join ' '
    $process = Start-Process -FilePath $resolvedExecutable -ArgumentList $argumentLine `
        -WorkingDirectory $resolvedOutput -NoNewWindow -PassThru `
        -RedirectStandardOutput $Run.StdoutPath -RedirectStandardError $Run.StderrPath
    # Touching Handle keeps ExitCode readable after the process exits.
    $null = $process.Handle
    $Run.Process = $process
    $Run.StartedAt = Get-Date
}

# Waits for both runs.  A skip condition is known as soon as one run exits
# with its marker, so the other is stopped early.
function Wait-Runs($Runs) {
    while ($true) {
        $pending = @($Runs | Where-Object { -not $_.Process.HasExited })
        foreach ($run in @($Runs | Where-Object { $_.Process.HasExited })) {
            $log = Get-RunLog $run
            $reason = $null
            if ($log.Contains($noDisplayMarker)) {
                $reason = "no display available to SDL (run $($run.Name), see $($run.StderrPath)); a desktop session is required"
            }
            elseif ($log.Contains($notInternalMarker)) {
                $reason = "--arcade-link-autopilot was rejected: the executable is not an internal (CTR_INTERNAL) build (run $($run.Name), see $($run.StderrPath))"
            }
            elseif ($log.Contains($unknownIdentityMarker)) {
                $reason = "the build identity is unknown (a build from a dirty tree), so the link cannot start (run $($run.Name), see $($run.StderrPath)); rebuild from a clean tree"
            }
            if ($null -ne $reason) {
                Stop-StartedRuns $Runs
                Exit-Skipped $reason
            }
        }
        if ($pending.Count -eq 0) {
            break
        }
        if ((Get-Date) -gt $deadline) {
            Stop-StartedRuns $Runs
            Exit-Failed "timed out after $TimeoutSeconds s waiting for run(s) $(($pending | ForEach-Object { $_.Name }) -join ', ') (logs in $resolvedOutput)"
        }
        Start-Sleep -Milliseconds 500
    }
    foreach ($run in $Runs) {
        $run.Process.WaitForExit()
    }
}

# Parses a report: the header, the agreed-match and validated lines in file
# order, and the end line.  k is the line's own race number, which must run
# 1..$races in order.
function Read-Report($Run) {
    $lines = @([System.IO.File]::ReadAllLines($Run.ReportPath))
    $report = [pscustomobject]@{
        Lines = $lines
        Agreed = @()
        Validated = @()
        Problems = @()
    }
    $expectedHeader = @('arcade link autopilot v1', "cab $($Run.CabNumber)", 'result PASS (0)')
    for ($i = 0; $i -lt $expectedHeader.Count; $i++) {
        $found = ''
        if ($i -lt $lines.Count) {
            $found = $lines[$i]
        }
        if ($found -ne $expectedHeader[$i]) {
            $report.Problems += "line $($i + 1) is '$found', expected '$($expectedHeader[$i])'"
        }
    }
    foreach ($line in $lines) {
        if ($line -match $agreedPattern) {
            $report.Agreed += [pscustomobject]@{ K = [int]$Matches[1]; Text = $Matches[2]; Line = $line }
        }
        elseif ($line -match $validatedPattern) {
            $report.Validated += [pscustomobject]@{
                K = [int]$Matches[1]; Launch = [int]$Matches[2]; Config = $Matches[3]; Plan = $Matches[4]; Bots = $Matches[5]
                Bank = $Matches[6]; Line = $line
            }
        }
        elseif ($line -match '^race ') {
            $report.Problems += "malformed race line '$line'"
        }
    }
    if ($report.Agreed.Count -ne $races) {
        $report.Problems += "$($report.Agreed.Count) agreed-match lines, expected $races"
    }
    if ($report.Validated.Count -ne $races) {
        $report.Problems += "$($report.Validated.Count) validated lines, expected $races (two races VALIDATED)"
    }
    for ($k = 0; $k -lt $report.Agreed.Count; $k++) {
        if ($report.Agreed[$k].K -ne ($k + 1)) {
            $report.Problems += "agreed-match line $($k + 1) is numbered race $($report.Agreed[$k].K)"
        }
    }
    for ($k = 0; $k -lt $report.Validated.Count; $k++) {
        if ($report.Validated[$k].K -ne ($k + 1)) {
            $report.Problems += "validated line $($k + 1) is numbered race $($report.Validated[$k].K)"
        }
    }
    if (($lines.Count -eq 0) -or ($lines[$lines.Count - 1] -ne "end races $races")) {
        $report.Problems += "the report does not end with 'end races $races'"
    }
    return $report
}

# The report's races against the run's own stdout: the k-th agreed-match log
# line and the k-th RL-12 line carry the report's k-th text, launch number,
# and digests.
function Compare-WithStdout($Run, $Report) {
    $found = @()
    $agreed = @()
    $validated = @()
    foreach ($line in ((Read-SharedText $Run.StdoutPath) -split "`r?`n")) {
        if ($line -match $stdoutAgreedPattern) {
            $agreed += $Matches[1]
        }
        elseif ($line -match $stdoutValidatedPattern) {
            $validated += [pscustomobject]@{ Launch = [int]$Matches[1]; Config = $Matches[2]; Plan = $Matches[3]; Bots = $Matches[4]; Bank = $Matches[5] }
        }
    }
    if ($agreed.Count -ne $Report.Agreed.Count) {
        $found += "run $($Run.Name): $($agreed.Count) agreed-match lines on stdout, $($Report.Agreed.Count) in the report"
    }
    if ($validated.Count -ne $Report.Validated.Count) {
        $found += "run $($Run.Name): $($validated.Count) RL-12 lines on stdout, $($Report.Validated.Count) in the report"
    }
    for ($k = 0; ($k -lt $agreed.Count) -and ($k -lt $Report.Agreed.Count); $k++) {
        if ($agreed[$k] -ne $Report.Agreed[$k].Text) {
            $found += "run $($Run.Name): race $($k + 1) agreed match differs between stdout ('$($agreed[$k])') and the report ('$($Report.Agreed[$k].Text)')"
        }
    }
    for ($k = 0; ($k -lt $validated.Count) -and ($k -lt $Report.Validated.Count); $k++) {
        foreach ($field in @('Launch', 'Config', 'Plan', 'Bots', 'Bank')) {
            if ($validated[$k].$field -ne $Report.Validated[$k].$field) {
                $found += "run $($Run.Name): race $($k + 1) $($field.ToLowerInvariant()) differs between stdout ($($validated[$k].$field)) and the report ($($Report.Validated[$k].$field))"
            }
        }
    }
    return $found
}

try {
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
    if ($TimeoutSeconds -lt 1) {
        Exit-Failed "invalid timeout $TimeoutSeconds s"
    }
    $resolvedExecutable = (Resolve-Path -LiteralPath $Executable -ErrorAction Stop).Path
    $resolvedOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
    [System.IO.Directory]::CreateDirectory($resolvedOutput) | Out-Null

    $specs = @(
        @{ Name = 'cab1'; Cab = 'cab1'; CabNumber = 1; Port = '7001'; Peer = '127.0.0.1:7002' },
        @{ Name = 'cab2'; Cab = 'cab2'; CabNumber = 2; Port = '7002'; Peer = '127.0.0.1:7001' })
    foreach ($spec in $specs) {
        $run = [pscustomobject]@{
            Name = $spec.Name
            Cab = $spec.Cab
            CabNumber = $spec.CabNumber
            Port = $spec.Port
            Peer = $spec.Peer
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

    Write-Output "arcade link launch check: two linked runs over loopback (cab1 port 7001, cab2 port 7002), two races each"
    Write-Output "executable: $resolvedExecutable"
    Write-Output "output:     $resolvedOutput"

    $checkStartedAt = Get-Date
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    foreach ($run in $runs) {
        Start-Run $run
    }
    Wait-Runs $runs

    $failures = @()
    foreach ($run in $runs) {
        $exitCode = $run.Process.ExitCode
        $seconds = [math]::Round(($run.Process.ExitTime - $run.StartedAt).TotalSeconds, 1)
        Write-Output ("run {0} exit {1} in {2} s" -f $run.Name, $exitCode, $seconds)
        if ($exitCode -ne 0) {
            $failures += "run $($run.Name) exited $exitCode; see $($run.StdoutPath), $($run.StderrPath), and $($run.ReportPath)"
        }
        if (-not (Test-Path -LiteralPath $run.ReportPath -PathType Leaf)) {
            $failures += "run $($run.Name) wrote no report $($run.ReportPath)"
        }
        elseif ($exitCode -ne 0) {
            foreach ($line in [System.IO.File]::ReadAllLines($run.ReportPath)) {
                if ($line -match '^(result|last screen|ticks) ') {
                    Write-Output "  $($run.Name) report: $line"
                }
            }
        }
    }
    Write-Output ("both runs done in {0} s" -f [math]::Round(((Get-Date) - $checkStartedAt).TotalSeconds, 1))
    if ($failures.Count -ne 0) {
        foreach ($failure in $failures) {
            Write-Output "FAIL: $failure"
        }
        Write-Output "arcade link launch check: FAIL"
        exit 1
    }

    $reports = @{}
    foreach ($run in $runs) {
        $report = Read-Report $run
        foreach ($problem in $report.Problems) {
            $failures += "report $($run.Name): $problem"
        }
        if ($report.Problems.Count -eq 0) {
            $failures += @(Compare-WithStdout $run $report)
        }
        $reports[$run.Name] = $report
    }
    if ($failures.Count -ne 0) {
        foreach ($failure in $failures) {
            Write-Output "FAIL: $failure"
        }
        Write-Output "arcade link launch check: FAIL"
        exit 1
    }

    # The k-th race of cab1 against the k-th race of cab2.
    $cab1 = $reports['cab1']
    $cab2 = $reports['cab2']
    for ($k = 0; $k -lt $races; $k++) {
        $number = $k + 1
        if ($cab1.Agreed[$k].Text -ne $cab2.Agreed[$k].Text) {
            $failures += "race $number agreed match differs: cab1 '$($cab1.Agreed[$k].Text)', cab2 '$($cab2.Agreed[$k].Text)'"
        }
        foreach ($field in @('Config', 'Plan', 'Bots', 'Bank')) {
            if ($cab1.Validated[$k].$field -ne $cab2.Validated[$k].$field) {
                $failures += "race $number $($field.ToLowerInvariant()) digest differs: cab1 $($cab1.Validated[$k].$field), cab2 $($cab2.Validated[$k].$field)"
            }
        }
        Write-Output "race ${number}: cab1 $($cab1.Agreed[$k].Line)"
        Write-Output "race ${number}: cab2 $($cab2.Agreed[$k].Line)"
        Write-Output "race ${number}: cab1 $($cab1.Validated[$k].Line)"
        Write-Output "race ${number}: cab2 $($cab2.Validated[$k].Line)"
    }
    # The rematch went through a new select: a new config.
    foreach ($pair in @(@('cab1', $cab1), @('cab2', $cab2))) {
        if ($pair[1].Validated[1].Config -eq $pair[1].Validated[0].Config) {
            $failures += "$($pair[0]): race 2's config digest equals race 1's ($($pair[1].Validated[0].Config)); the rematch did not go through a new select"
        }
    }
    if ($failures.Count -eq 0) {
        Write-Output "races 1 and 2: agreed match, config, plan, bots, and bank equal across cab1 and cab2 (paired by race order; launch numbers cab1 $($cab1.Validated[0].Launch),$($cab1.Validated[1].Launch) cab2 $($cab2.Validated[0].Launch),$($cab2.Validated[1].Launch))"
        Write-Output "race 2 config differs from race 1 config on both cabinets (a new select)"
        Write-Output "both reports agree with their stdout (agreed-match and RL-12 lines)"
    }
    Write-Output ''
    if ($failures.Count -ne 0) {
        foreach ($failure in $failures) {
            Write-Output "FAIL: $failure"
        }
        Write-Output "arcade link launch check: FAIL ($($failures.Count) failure(s); reports in $resolvedOutput)"
        exit 1
    }
    Write-Output "arcade link launch check: PASS"
    exit 0
}
finally {
    # On every exit and on an interrupt: no ctr_native this check started
    # outlives it.
    Stop-StartedRuns $runs
}
