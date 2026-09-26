[CmdletBinding()]
param(
    # Internal ctr_native build that accepts --arcade-link-autopilot.
    [Parameter(Mandatory = $true)]
    [string]$Executable,

    # Absolute directory for the reports and the stdout/stderr logs.  Both
    # runs use it as their working directory; the game itself still writes the
    # gitignored `Crash Team Racing.log` in its base directory.
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    # User-supplied disc image.  The check skips (exit 77) when it is absent.
    # Defaults to <repo>\assets\ctr-u.bin, resolved below: Windows PowerShell
    # 5.1 leaves $PSScriptRoot empty inside the param defaults of a
    # [CmdletBinding()] script run with -File.
    [string]$AssetsFile,

    # Seconds both runs together may take.
    [int]$TimeoutSeconds = 180
)

# Discovery live link gate (docs/DISCOVERY_MILESTONE.md DISC-12, slice
# DISC-S4).  Starts two internal ctr_native processes at once in discovery
# mode: no peer and no fixed seat, each beaconing at the other's discovery
# port on loopback, on ports outside 7001/7002 (package_arcade_smoke),
# 7101/7102 (arcade_link_launch), 7201-7204 (arcade_solo_race), and
# 48000-48629 (the fast suite's socket tests):
#   a  --arcade-link auto --arcade-link-port 7301
#      --arcade-discovery-port 7303 --arcade-discovery-target 127.0.0.1:7304
#   b  --arcade-link auto --arcade-link-port 7302
#      --arcade-discovery-port 7304 --arcade-discovery-target 127.0.0.1:7303
# each with --arcade-link-autopilot <report> --arcade-link-autopilot-one-race
# --arcade-link-autopilot-race-ticks 900, which drives the link host's own
# inputs (never a pad; include/platform/native_arcade_link_autopilot.h, the
# one-race mode): START on the attract screen, the LOBBY until the
# discovered pairing links the two, the linked select, one lockstep race,
# RESULTS, EXIT, and the title, where the autopilot passes and the process
# exits.  Both seats are auto on one IPv4, so the election (DISC-8) makes the
# lower link port, a on 7301, cab1 and b cab2.
# Per run it requires:
#   - exit code 0;
#   - the report: "arcade link autopilot v3", "cab <n>" (a 1, b 2),
#     "mode one-race", "result PASS (0)", "race ticks 900", "freeze tick 0",
#     "desync tick 0", exactly one "race 1 agreed match ..." line, one
#     "race 1 validated launch <n> ..." line, "race 1 end reason FINISHED", no
#     other race line, and last "end races 1";
#   - stdout: the autopilot's "one-race mode" and "race tick limit 900"
#     lines; the discovery socket's "listening on port <own discovery port>"
#     line; exactly one "paired with" line, naming the other run's link
#     address and this run's seat ("paired with 127.0.0.1:7302 as cab1" for a,
#     "paired with 127.0.0.1:7301 as cab2" for b); exactly one agreed match
#     line, equal to the report's; exactly one RL-12 "race <n> validated"
#     line whose launch and digests equal the report's; no "out of sync" line;
#     and the autopilot's PASS line (1 race started, validated, and ended).
# Across the runs: the agreed match text and the config, plan, bots, and bank
# digests are equal.
#
# Skips (77) without the disc image, without a display, with a non-internal
# build (the option is rejected), or with an unknown build identity (a build
# from a dirty tree: the link cannot start).  A skip is not a pass.  Every
# ctr_native the check started is stopped when the check exits or is
# interrupted.  Everything is written under -OutputDirectory.
#
# Exit codes: 0 pass, 1 fail, 77 skipped (ctest SKIP_RETURN_CODE).
$skipExitCode = 77
$noDisplayMarker = 'No displays available'
$notInternalMarker = '--arcade-link-autopilot is available in internal builds only.'
# main.c prints this one line both for an unknown build identity (a dirty-tree
# build) and for a missing content identity.
$unknownIdentityMarker = 'arcade link requires a known build and content identity.'
# The race tick cap on both runs: a 900-tick race (30 s) ends "race tick
# limit" unless it finishes first.
$raceTickCap = 900
$reportAgreedPattern = '^race 1 (agreed match track [0-9]+ laps [0-9]+ seed 0x[0-9A-F]{16} slots( [0-9]+){8} \([12B-]{8}\))$'
$reportValidatedPattern = '^race 1 validated launch ([0-9]+) config ([0-9a-f]{64}) plan ([0-9a-f]{64}) bots ([0-9a-f]{64}) bank ([0-9a-f]{64})$'
$stdoutOneRaceModeLine = '[CTR Native] arcade link autopilot: one-race mode'
$stdoutRaceTickLimitLine = "[CTR Native] arcade link autopilot: race tick limit $raceTickCap"
$stdoutPairedPattern = '^\[CTR Native\] arcade discovery: paired with '
$stdoutAgreedPattern = '^\[CTR Native\] arcade link: (agreed match .*)$'
$stdoutValidatedPattern = '^\[CTR Native\] arcade link: race ([0-9]+) validated config ([0-9a-f]{64}) plan ([0-9a-f]{64}) bots ([0-9a-f]{64}) bank ([0-9a-f]{64})$'
$stdoutOutOfSyncPattern = '^\[CTR Native\] arcade link: race ([0-9]+) out of sync '
$stdoutPassPattern = '^\[CTR Native\] arcade link autopilot: PASS after ([0-9]+) ticks \(1 races started, 1 validated, 1 ended; last screen [A-Z_]+ end reason [A-Z_]+\); exit code 0$'
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
# moment after the process exited, or the process is still running.
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
    $arguments = @('--arcade-link', 'auto', '--arcade-link-port', $Run.LinkPort,
        '--arcade-discovery-port', $Run.DiscoveryPort, '--arcade-discovery-target', $Run.DiscoveryTarget,
        '--arcade-link-autopilot', $Run.ReportPath, '--arcade-link-autopilot-one-race', '--arcade-link-autopilot-race-ticks', $raceTickCap)
    $argumentLine = ($arguments | ForEach-Object { ConvertTo-ProcessArgument $_ }) -join ' '
    # A run that cannot start fails the check at once, not at the timeout.
    $process = $null
    try {
        $process = Start-Process -FilePath $resolvedExecutable -ArgumentList $argumentLine `
            -WorkingDirectory $resolvedOutput -NoNewWindow -PassThru `
            -RedirectStandardOutput $Run.StdoutPath -RedirectStandardError $Run.StderrPath -ErrorAction Stop
    }
    catch {
        Exit-Failed "could not start run $($Run.Name): $($_.Exception.Message)"
    }
    if ($null -eq $process) {
        Exit-Failed "could not start run $($Run.Name): Start-Process returned no process"
    }
    # Touching Handle keeps ExitCode readable after the process exits.
    $null = $process.Handle
    $Run.Process = $process
    $Run.StartedAt = Get-Date
}

# Waits for both runs.  A skip condition is known as soon as one run exits
# with its marker; a run exiting nonzero fails at once and the other is
# stopped.
function Wait-Runs($All) {
    while ($true) {
        foreach ($run in @($All | Where-Object { $_.Process.HasExited })) {
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
                Stop-StartedRuns $All
                Exit-Skipped $reason
            }
            $run.Process.WaitForExit()
            if ($run.Process.ExitCode -ne 0) {
                Stop-StartedRuns $All
                Write-ReportSummary $run
                Exit-Failed "run $($run.Name) exited $($run.Process.ExitCode); the other run was stopped (see $($run.StdoutPath), $($run.StderrPath), and $($run.ReportPath))"
            }
        }
        if (@($All | Where-Object { -not $_.Process.HasExited }).Count -eq 0) {
            break
        }
        if ((Get-Date) -gt $deadline) {
            Stop-StartedRuns $All
            $pending = @($All | Where-Object { -not $_.Process.HasExited } | ForEach-Object { $_.Name })
            Exit-Failed "timed out after $TimeoutSeconds s waiting for run(s) $($pending -join ', ') (logs in $resolvedOutput)"
        }
        Start-Sleep -Milliseconds 500
    }
}

function Write-ReportSummary($Run) {
    if (Test-Path -LiteralPath $Run.ReportPath -PathType Leaf) {
        foreach ($line in [System.IO.File]::ReadAllLines($Run.ReportPath)) {
            if ($line -match '^(cab|mode|result|last screen|ticks|race ticks|race [0-9]+ end reason) ') {
                Write-Output "  $($Run.Name) report: $line"
            }
        }
    }
}

# The report of one run: its problems, its race 1 agreed match text, and its
# race 1 validated line.
function Read-Report($Run, $Failures) {
    $result = [pscustomobject]@{ Agreed = $null; Validated = $null }
    if (-not (Test-Path -LiteralPath $Run.ReportPath -PathType Leaf)) {
        [void]$Failures.Add("run $($Run.Name) wrote no report $($Run.ReportPath)")
        return $result
    }
    $lines = @([System.IO.File]::ReadAllLines($Run.ReportPath))
    $expectedHeader = @('arcade link autopilot v3', "cab $($Run.CabNumber)", 'mode one-race', 'result PASS (0)')
    for ($i = 0; $i -lt $expectedHeader.Count; $i++) {
        $found = ''
        if ($i -lt $lines.Count) {
            $found = $lines[$i]
        }
        if ($found -ne $expectedHeader[$i]) {
            [void]$Failures.Add("report $($Run.Name): line $($i + 1) is '$found', expected '$($expectedHeader[$i])'")
        }
    }
    foreach ($required in @("race ticks $raceTickCap", 'freeze tick 0', 'desync tick 0', 'race 1 end reason FINISHED')) {
        $count = @($lines | Where-Object { $_ -ceq $required }).Count
        if ($count -ne 1) {
            [void]$Failures.Add("report $($Run.Name): $count '$required' lines, expected 1")
        }
    }
    $agreedLines = @($lines | Where-Object { $_ -match $reportAgreedPattern })
    if ($agreedLines.Count -ne 1) {
        [void]$Failures.Add("report $($Run.Name): $($agreedLines.Count) 'race 1 agreed match' lines, expected 1")
    }
    else {
        $null = $agreedLines[0] -match $reportAgreedPattern
        $result.Agreed = $Matches[1]
    }
    $validatedLines = @($lines | Where-Object { $_ -match $reportValidatedPattern })
    if ($validatedLines.Count -ne 1) {
        [void]$Failures.Add("report $($Run.Name): $($validatedLines.Count) 'race 1 validated' lines, expected 1")
    }
    else {
        $null = $validatedLines[0] -match $reportValidatedPattern
        $result.Validated = [pscustomobject]@{ Launch = [int]$Matches[1]; Config = $Matches[2]; Plan = $Matches[3]; Bots = $Matches[4]; Bank = $Matches[5] }
    }
    foreach ($line in $lines) {
        if (($line -match '^race ') -and ($line -notmatch '^race ticks ') -and ($line -notmatch $reportAgreedPattern) -and
            ($line -notmatch $reportValidatedPattern) -and ($line -cne 'race 1 end reason FINISHED')) {
            [void]$Failures.Add("report $($Run.Name): unexpected race line '$line'")
        }
    }
    if (($lines.Count -eq 0) -or ($lines[$lines.Count - 1] -ne 'end races 1')) {
        [void]$Failures.Add("report $($Run.Name): the report does not end with 'end races 1'")
    }
    return $result
}

# The stdout of one run against its report; returns its RL-12 validated line.
function Test-Stdout($Run, $Report, $Failures) {
    $lines = @((Read-SharedText $Run.StdoutPath) -split "`r?`n")
    $prefix = "run $($Run.Name)"
    $listeningPrefix = "[CTR Native] arcade discovery: listening on port $($Run.DiscoveryPort), "
    foreach ($required in @($stdoutOneRaceModeLine, $stdoutRaceTickLimitLine)) {
        $count = @($lines | Where-Object { $_ -ceq $required }).Count
        if ($count -ne 1) {
            [void]$Failures.Add("${prefix}: $count '$required' lines on stdout, expected 1")
        }
    }
    $listening = @($lines | Where-Object { $_.StartsWith($listeningPrefix) })
    if ($listening.Count -ne 1) {
        [void]$Failures.Add("${prefix}: $($listening.Count) '${listeningPrefix}...' lines on stdout, expected 1 (the discovery socket did not open)")
    }
    # The pairing: once, the other run's link address, this run's seat.
    $expectedPaired = "[CTR Native] arcade discovery: paired with 127.0.0.1:$($Run.PeerLinkPort) as cab$($Run.CabNumber)"
    $paired = @($lines | Where-Object { $_ -match $stdoutPairedPattern })
    if (($paired.Count -ne 1) -or ($paired[0] -cne $expectedPaired)) {
        [void]$Failures.Add("${prefix}: the 'paired with' lines on stdout are '$($paired -join ''', ''')', expected exactly '$expectedPaired'")
    }
    $agreed = @($lines | Where-Object { $_ -match $stdoutAgreedPattern })
    if ($agreed.Count -ne 1) {
        [void]$Failures.Add("${prefix}: $($agreed.Count) agreed match lines on stdout, expected 1")
    }
    else {
        $null = $agreed[0] -match $stdoutAgreedPattern
        if (($null -ne $Report.Agreed) -and ($Matches[1] -cne $Report.Agreed)) {
            [void]$Failures.Add("${prefix}: the agreed match differs between stdout ('$($Matches[1])') and the report ('$($Report.Agreed)')")
        }
    }
    $stdoutValidated = $null
    $validatedLines = @($lines | Where-Object { $_ -match $stdoutValidatedPattern })
    if ($validatedLines.Count -ne 1) {
        [void]$Failures.Add("${prefix}: $($validatedLines.Count) RL-12 'race <n> validated' lines on stdout, expected 1")
    }
    else {
        $null = $validatedLines[0] -match $stdoutValidatedPattern
        $stdoutValidated = [pscustomobject]@{ Launch = [int]$Matches[1]; Config = $Matches[2]; Plan = $Matches[3]; Bots = $Matches[4]; Bank = $Matches[5] }
        if ($null -ne $Report.Validated) {
            foreach ($field in @('Launch', 'Config', 'Plan', 'Bots', 'Bank')) {
                if ($stdoutValidated.$field -ne $Report.Validated.$field) {
                    [void]$Failures.Add("${prefix}: $($field.ToLowerInvariant()) differs between stdout ($($stdoutValidated.$field)) and the report ($($Report.Validated.$field))")
                }
            }
        }
    }
    foreach ($line in $lines) {
        if ($line -match $stdoutOutOfSyncPattern) {
            [void]$Failures.Add("${prefix}: an out of sync line: '$line'")
        }
    }
    if (@($lines | Where-Object { $_ -match $stdoutPassPattern }).Count -ne 1) {
        [void]$Failures.Add("${prefix}: no autopilot PASS line (1 race started, validated, and ended) on stdout")
    }
    Write-Output ("{0}: {1}" -f $Run.Name, (($paired | Select-Object -First 1) -replace '^\[CTR Native\] arcade discovery: ', ''))
    return $stdoutValidated
}

try {
    $checkStartedAt = Get-Date
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
        @{ Name = 'a'; CabNumber = 1; LinkPort = '7301'; PeerLinkPort = '7302'; DiscoveryPort = '7303'; DiscoveryTarget = '127.0.0.1:7304' },
        @{ Name = 'b'; CabNumber = 2; LinkPort = '7302'; PeerLinkPort = '7301'; DiscoveryPort = '7304'; DiscoveryTarget = '127.0.0.1:7303' })
    foreach ($spec in $specs) {
        $run = [pscustomobject]@{
            Name = $spec.Name
            CabNumber = $spec.CabNumber
            LinkPort = $spec.LinkPort
            PeerLinkPort = $spec.PeerLinkPort
            DiscoveryPort = $spec.DiscoveryPort
            DiscoveryTarget = $spec.DiscoveryTarget
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

    Write-Output "arcade discovery link check: two discovery-mode runs, seat auto, no peer (a link 7301 discovery 7303 -> 7304, b link 7302 discovery 7304 -> 7303), one linked race, race tick cap $raceTickCap"
    Write-Output "executable: $resolvedExecutable"
    Write-Output "output:     $resolvedOutput"

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    foreach ($run in $runs) {
        Start-Run $run
    }
    Wait-Runs $runs

    $failures = New-Object System.Collections.ArrayList
    $reports = @{}
    $validated = @{}
    foreach ($run in $runs) {
        Write-Output ("run {0} exit {1} in {2} s" -f $run.Name, $run.Process.ExitCode, [math]::Round(($run.Process.ExitTime - $run.StartedAt).TotalSeconds, 1))
        $reports[$run.Name] = Read-Report $run $failures
        $validated[$run.Name] = Test-Stdout $run $reports[$run.Name] $failures
    }

    # The two cabinets raced the same match.
    if (($null -ne $reports['a'].Agreed) -and ($null -ne $reports['b'].Agreed) -and ($reports['a'].Agreed -cne $reports['b'].Agreed)) {
        [void]$failures.Add("the agreed match differs: a '$($reports['a'].Agreed)', b '$($reports['b'].Agreed)'")
    }
    if (($null -ne $validated['a']) -and ($null -ne $validated['b'])) {
        foreach ($field in @('Config', 'Plan', 'Bots', 'Bank')) {
            if ($validated['a'].$field -ne $validated['b'].$field) {
                [void]$failures.Add("the $($field.ToLowerInvariant()) digest differs: a $($validated['a'].$field), b $($validated['b'].$field)")
            }
        }
    }

    Write-Output ''
    if ($failures.Count -ne 0) {
        foreach ($failure in $failures) {
            Write-Output "FAIL: $failure"
        }
        foreach ($run in $runs) {
            Write-ReportSummary $run
        }
        Write-Output "arcade discovery link check: FAIL ($($failures.Count) failure(s); reports and logs in $resolvedOutput)"
        Write-Output ("arcade discovery link check: total time {0} s" -f [math]::Round(((Get-Date) - $checkStartedAt).TotalSeconds, 1))
        exit 1
    }
    Write-Output "agreed match (both): $($reports['a'].Agreed)"
    Write-Output "both runs: discovered each other, elected a (7301) cab1 and b (7302) cab2, linked, raced one validated race on the same match and digests to FINISHED, and exited"
    Write-Output "arcade discovery link check: PASS"
    Write-Output ("arcade discovery link check: total time {0} s" -f [math]::Round(((Get-Date) - $checkStartedAt).TotalSeconds, 1))
    exit 0
}
finally {
    # On every exit and on an interrupt: no ctr_native this check started
    # outlives it.
    Stop-StartedRuns $runs
}
