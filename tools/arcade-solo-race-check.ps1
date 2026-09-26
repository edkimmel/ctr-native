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
    [int]$TimeoutSeconds = 300
)

# Solo live race gate (docs/SOLO_CAB_MILESTONE.md SOLO-S4).  Starts two
# independent one-cabinet internal ctr_native processes at once, each with a
# silent peer (nothing listens on its peer port), on loopback ports outside
# 7001/7002 (package_arcade_smoke), 7101/7102 (arcade_link_launch), and
# 48000-48600 (the fast suite's socket tests):
#   cab1  --arcade-link cab1 --arcade-link-port 7201 --arcade-link-peer 127.0.0.1:7202
#   cab2  --arcade-link cab2 --arcade-link-port 7203 --arcade-link-peer 127.0.0.1:7204
# each with --arcade-link-autopilot <report> --arcade-link-autopilot-solo
# --arcade-link-autopilot-race-ticks 900, which drives the link host's own
# inputs (never a pad; include/platform/native_arcade_link_autopilot.h, the
# solo mode): START on the attract screen, the LOBBY until the solo offer
# (SOLO-2: the peer is never heard), CONFIRM, the one-human solo select, one
# solo race on the race drive's local mode with the autopilot's steering pad
# as the local sample, the solo RESULTS, the LOBBY row, and the LOBBY again,
# where the autopilot passes and the process exits.  The two runs share
# nothing but the machine: each is its own solo cabinet (cab2 races solo on
# its own seat, SOLO-6: the human is CAB1_HUMAN in slot 0 on either seat).
# Per run it requires:
#   - exit code 0;
#   - the report: "arcade link autopilot v3", "cab <n>", "mode solo",
#     "result PASS (0)", "last screen LOBBY ...", "race ticks 900",
#     "freeze tick 0", "desync tick 0", exactly one "race 1 validated launch
#     <n> ..." line, "race 1 end reason FINISHED", no agreed match line, and
#     last "end races 1";
#   - stdout: the autopilot's "solo mode" and "race tick limit 900" lines;
#     exactly one solo launch line of the race caller
#     (game/MAIN/MainArcadeRaceLaunch.c: "race <n> launched (track <t> laps
#     <l>, solo)"), exactly one RL-12 "race <n> validated config ..." line of
#     the same launch number whose digests equal the report's, exactly one
#     drive end of that race ("race tick limit" at race tick 900, or "end of
#     race" or "finish grace" at or below it), the per-tick digest lines of the
#     race contiguous from race tick 0 to the drive end tick, one "race <m>
#     ended (reason 1)" line (FINISHED), no agreed match line, no "out of
#     sync" line, the autopilot's "race 1 started (solo)" and its return to
#     the LOBBY ("solo RESULTS: LOBBY confirmed, back in the LOBBY"), then its
#     PASS line with last screen LOBBY.
#
# Skips (77) without the disc image, without a display, with a non-internal
# build (the option is rejected), or with an unknown build identity (a build
# from a dirty tree: the link, and so solo, cannot start at all; this is the
# link's own requirement, not a comparison of two cabinets).  A skip is not a
# pass.  Every ctr_native the check started is stopped when the check exits or
# is interrupted.  Everything is written under -OutputDirectory.
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
$finishedEndKinds = @('end of race', 'finish grace', 'race tick limit')
$reportValidatedPattern = '^race 1 validated launch ([0-9]+) config ([0-9a-f]{64}) plan ([0-9a-f]{64}) bots ([0-9a-f]{64}) bank ([0-9a-f]{64})$'
$stdoutSoloModeLine = '[CTR Native] arcade link autopilot: solo mode'
$stdoutRaceTickLimitLine = "[CTR Native] arcade link autopilot: race tick limit $raceTickCap"
$stdoutLaunchPattern = '^\[CTR Native\] arcade link: race ([0-9]+) launched \(track ([0-9]+) laps ([0-9]+)(, solo)?\)$'
$stdoutValidatedPattern = '^\[CTR Native\] arcade link: race ([0-9]+) validated config ([0-9a-f]{64}) plan ([0-9a-f]{64}) bots ([0-9a-f]{64}) bank ([0-9a-f]{64})$'
$stdoutDriveEndPattern = '^\[CTR Native\] arcade link: race ([0-9]+) drive end: (.*) at race tick ([0-9]+)$'
$stdoutTickPattern = '^\[CTR Native\] arcade link: race ([0-9]+) race tick ([0-9]+) digests v4 '
$stdoutEndedPattern = '^\[CTR Native\] arcade link: race ([0-9]+) ended \(reason ([0-9]+)\); foreign bundles dropped ([0-9]+)$'
$stdoutAgreedPrefix = '[CTR Native] arcade link: agreed match '
$stdoutOutOfSyncPattern = '^\[CTR Native\] arcade link: race ([0-9]+) out of sync '
$stdoutSoloStartedLine = '[CTR Native] arcade link autopilot: race 1 started (solo)'
$stdoutLobbyLine = '[CTR Native] arcade link autopilot: solo RESULTS: LOBBY confirmed, back in the LOBBY'
$stdoutPassPattern = '^\[CTR Native\] arcade link autopilot: PASS after ([0-9]+) ticks \(1 races started, 1 validated, 1 ended; last screen LOBBY end reason [A-Z_]+\); exit code 0$'
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
    $arguments = @('--arcade-link', $Run.Cab, '--arcade-link-port', $Run.Port, '--arcade-link-peer', $Run.Peer,
        '--arcade-link-autopilot', $Run.ReportPath, '--arcade-link-autopilot-solo', '--arcade-link-autopilot-race-ticks', $raceTickCap)
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
            if ($line -match '^(mode|result|last screen|ticks|race ticks|race [0-9]+ end reason) ') {
                Write-Output "  $($Run.Name) report: $line"
            }
        }
    }
}

# The report of one run: its problems, and its race 1 validated line.
function Read-Report($Run, $Failures) {
    $validated = $null
    if (-not (Test-Path -LiteralPath $Run.ReportPath -PathType Leaf)) {
        [void]$Failures.Add("run $($Run.Name) wrote no report $($Run.ReportPath)")
        return $null
    }
    $lines = @([System.IO.File]::ReadAllLines($Run.ReportPath))
    $expectedHeader = @('arcade link autopilot v3', "cab $($Run.CabNumber)", 'mode solo', 'result PASS (0)')
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
    if (@($lines | Where-Object { $_ -match '^last screen LOBBY end reason [A-Z_]+$' }).Count -ne 1) {
        [void]$Failures.Add("report $($Run.Name): no 'last screen LOBBY end reason ...' line (the run must pass on the LOBBY)")
    }
    $validatedLines = @($lines | Where-Object { $_ -match $reportValidatedPattern })
    if ($validatedLines.Count -ne 1) {
        [void]$Failures.Add("report $($Run.Name): $($validatedLines.Count) 'race 1 validated' lines, expected 1")
    }
    else {
        $null = $validatedLines[0] -match $reportValidatedPattern
        $validated = [pscustomobject]@{ Launch = [int]$Matches[1]; Config = $Matches[2]; Plan = $Matches[3]; Bots = $Matches[4]; Bank = $Matches[5] }
    }
    foreach ($line in $lines) {
        if ($line -match 'agreed match') {
            [void]$Failures.Add("report $($Run.Name): an agreed match line in a solo run: '$line'")
        }
        elseif (($line -match '^race ') -and ($line -notmatch '^race ticks ') -and ($line -notmatch $reportValidatedPattern) -and ($line -cne 'race 1 end reason FINISHED')) {
            [void]$Failures.Add("report $($Run.Name): unexpected race line '$line'")
        }
    }
    if (($lines.Count -eq 0) -or ($lines[$lines.Count - 1] -ne 'end races 1')) {
        [void]$Failures.Add("report $($Run.Name): the report does not end with 'end races 1'")
    }
    return $validated
}

# The stdout of one run against its report's validated line.
function Test-Stdout($Run, $Validated, $Failures) {
    $lines = @((Read-SharedText $Run.StdoutPath) -split "`r?`n")
    $prefix = "run $($Run.Name)"
    foreach ($required in @($stdoutSoloModeLine, $stdoutRaceTickLimitLine, $stdoutSoloStartedLine, $stdoutLobbyLine)) {
        $count = @($lines | Where-Object { $_ -ceq $required }).Count
        if ($count -ne 1) {
            [void]$Failures.Add("${prefix}: $count '$required' lines on stdout, expected 1")
        }
    }
    $launches = @($lines | Where-Object { $_ -match $stdoutLaunchPattern })
    $soloLaunches = @($launches | Where-Object { $_.EndsWith(', solo)') })
    if (($launches.Count -ne 1) -or ($soloLaunches.Count -ne 1)) {
        [void]$Failures.Add("${prefix}: $($launches.Count) race launch lines on stdout ($($soloLaunches.Count) solo), expected exactly one solo launch ('race <n> launched (track <t> laps <l>, solo)')")
        return
    }
    $null = $soloLaunches[0] -match $stdoutLaunchPattern
    $launch = [int]$Matches[1]
    $track = $Matches[2]
    $laps = $Matches[3]
    $validatedLines = @($lines | Where-Object { $_ -match $stdoutValidatedPattern })
    if ($validatedLines.Count -ne 1) {
        [void]$Failures.Add("${prefix}: $($validatedLines.Count) RL-12 'race <n> validated' lines on stdout, expected 1")
        return
    }
    $null = $validatedLines[0] -match $stdoutValidatedPattern
    $stdoutValidated = [pscustomobject]@{ Launch = [int]$Matches[1]; Config = $Matches[2]; Plan = $Matches[3]; Bots = $Matches[4]; Bank = $Matches[5] }
    if ($stdoutValidated.Launch -ne $launch) {
        [void]$Failures.Add("${prefix}: the validated race is launch $($stdoutValidated.Launch), the solo launch is $launch")
    }
    if ($null -ne $Validated) {
        foreach ($field in @('Launch', 'Config', 'Plan', 'Bots', 'Bank')) {
            if ($stdoutValidated.$field -ne $Validated.$field) {
                [void]$Failures.Add("${prefix}: $($field.ToLowerInvariant()) differs between stdout ($($stdoutValidated.$field)) and the report ($($Validated.$field))")
            }
        }
    }
    $driveEnds = @($lines | Where-Object { $_ -match $stdoutDriveEndPattern })
    if ($driveEnds.Count -ne 1) {
        [void]$Failures.Add("${prefix}: $($driveEnds.Count) drive end lines on stdout, expected 1")
        return
    }
    $null = $driveEnds[0] -match $stdoutDriveEndPattern
    $endLaunch = [int]$Matches[1]
    $endKind = $Matches[2]
    $endTick = [int]$Matches[3]
    if ($endLaunch -ne $launch) {
        [void]$Failures.Add("${prefix}: the drive end is of launch $endLaunch, the solo launch is $launch")
    }
    if ($finishedEndKinds -notcontains $endKind) {
        [void]$Failures.Add("${prefix}: the drive ended '$endKind', expected one of $($finishedEndKinds -join ', ')")
    }
    elseif ((($endKind -eq 'race tick limit') -and ($endTick -ne $raceTickCap)) -or ($endTick -gt $raceTickCap)) {
        [void]$Failures.Add("${prefix}: the drive ended '$endKind' at race tick $endTick (cap $raceTickCap)")
    }
    # The local drive ran every race tick: one per-tick digest line per race
    # tick, from 0 to the drive end tick, in order.
    $tickLines = @($lines | Where-Object { $_ -match $stdoutTickPattern } | ForEach-Object { $null = $_ -match $stdoutTickPattern; [pscustomobject]@{ Launch = [int]$Matches[1]; Tick = [int]$Matches[2] } })
    $raceTicks = @($tickLines | Where-Object { $_.Launch -eq $launch })
    if ($raceTicks.Count -ne ($endTick + 1)) {
        [void]$Failures.Add("${prefix}: $($raceTicks.Count) per-tick digest lines of launch $launch, expected $($endTick + 1) (race ticks 0..$endTick)")
    }
    else {
        for ($i = 0; $i -lt $raceTicks.Count; $i++) {
            if ($raceTicks[$i].Tick -ne $i) {
                [void]$Failures.Add("${prefix}: per-tick digest line $($i + 1) is race tick $($raceTicks[$i].Tick), expected $i")
                break
            }
        }
    }
    if ($tickLines.Count -ne $raceTicks.Count) {
        [void]$Failures.Add("${prefix}: per-tick digest lines of another launch than $launch")
    }
    $ended = @($lines | Where-Object { $_ -match $stdoutEndedPattern })
    if (($ended.Count -ne 1) -or ($ended[0] -notmatch '\(reason 1\)')) {
        [void]$Failures.Add("${prefix}: the 'ended (reason r)' lines on stdout are '$($ended -join ''', ''')', expected one with reason 1 (FINISHED)")
    }
    foreach ($line in $lines) {
        if ($line.StartsWith($stdoutAgreedPrefix)) {
            [void]$Failures.Add("${prefix}: an agreed match line in a solo run: '$line'")
        }
        elseif ($line -match $stdoutOutOfSyncPattern) {
            [void]$Failures.Add("${prefix}: an out of sync line in a solo run: '$line'")
        }
    }
    $passes = @($lines | Where-Object { $_ -match $stdoutPassPattern })
    if ($passes.Count -ne 1) {
        [void]$Failures.Add("${prefix}: no autopilot PASS line with last screen LOBBY on stdout")
    }
    $lobbyAt = [array]::IndexOf($lines, $stdoutLobbyLine)
    $endAt = [array]::IndexOf($lines, $driveEnds[0])
    if (($lobbyAt -ge 0) -and ($endAt -ge 0) -and ($lobbyAt -lt $endAt)) {
        [void]$Failures.Add("${prefix}: the return to the LOBBY precedes the drive end on stdout")
    }
    Write-Output ("{0}: solo race launch {1} (track {2} laps {3}), drive end '{4}' at race tick {5}, {6} per-tick lines, back in the LOBBY" -f
        $Run.Name, $launch, $track, $laps, $endKind, $endTick, $raceTicks.Count)
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
        @{ Name = 'cab1'; Cab = 'cab1'; CabNumber = 1; Port = '7201'; Peer = '127.0.0.1:7202' },
        @{ Name = 'cab2'; Cab = 'cab2'; CabNumber = 2; Port = '7203'; Peer = '127.0.0.1:7204' })
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

    Write-Output "arcade solo race check: two independent solo runs, each with a silent peer (cab1 port 7201 peer 7202, cab2 port 7203 peer 7204), race tick cap $raceTickCap"
    Write-Output "executable: $resolvedExecutable"
    Write-Output "output:     $resolvedOutput"

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    foreach ($run in $runs) {
        Start-Run $run
    }
    Wait-Runs $runs

    $failures = New-Object System.Collections.ArrayList
    foreach ($run in $runs) {
        Write-Output ("run {0} exit {1} in {2} s" -f $run.Name, $run.Process.ExitCode, [math]::Round(($run.Process.ExitTime - $run.StartedAt).TotalSeconds, 1))
        $validated = Read-Report $run $failures
        Test-Stdout $run $validated $failures
    }

    Write-Output ''
    if ($failures.Count -ne 0) {
        foreach ($failure in $failures) {
            Write-Output "FAIL: $failure"
        }
        foreach ($run in $runs) {
            Write-ReportSummary $run
        }
        Write-Output "arcade solo race check: FAIL ($($failures.Count) failure(s); reports and logs in $resolvedOutput)"
        Write-Output ("arcade solo race check: total time {0} s" -f [math]::Round(((Get-Date) - $checkStartedAt).TotalSeconds, 1))
        exit 1
    }
    Write-Output "both runs: the solo offer, the solo select, one validated solo race on the local drive, solo RESULTS FINISHED, and back in the LOBBY; no agreed match"
    Write-Output "arcade solo race check: PASS"
    Write-Output ("arcade solo race check: total time {0} s" -f [math]::Round(((Get-Date) - $checkStartedAt).TotalSeconds, 1))
    exit 0
}
finally {
    # On every exit and on an interrupt: no ctr_native this check started
    # outlives it.
    Stop-StartedRuns $runs
}
