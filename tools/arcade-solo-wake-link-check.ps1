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

    # Seconds both runs together may take, cab1's start to both exits.
    [int]$TimeoutSeconds = 210
)

# Solo wake link gate (docs/SOLO_CAB_MILESTONE.md risk 9: "peer wakes during
# solo, then links from the LOBBY").  Two internal ctr_native processes on
# loopback ports outside 7001-7004 (package_arcade_smoke), 7101/7102
# (arcade_link_launch), 7201-7204 (arcade_solo_race), 7301-7304
# (arcade_discovery_link), and 48000-48629 (the fast suite's socket tests):
#   cab1  --arcade-link cab1 --arcade-link-port 7401 --arcade-link-peer 127.0.0.1:7402
#         --arcade-link-autopilot <report> --arcade-link-autopilot-solo-then-link
#   cab2  --arcade-link cab2 --arcade-link-port 7402 --arcade-link-peer 127.0.0.1:7401
#         --arcade-link-autopilot <report> --arcade-link-autopilot-one-race
# both with --arcade-link-autopilot-race-ticks 1200.  cab1 starts alone: its
# peer is silent, so the LOBBY offers solo (SOLO-2), and the autopilot takes
# it (include/platform/native_arcade_link_autopilot.h, the solo-then-link
# mode) and races solo on the race drive's local mode.  This check starts cab2
# only once cab1's stdout shows its solo race's race tick 0 per-tick digest
# line (stdout is fully buffered, so it is seen in 4 KB chunks, a little
# late).  cab2 boots to its own LOBBY and sends its HELLO; cab1's listen-only
# link (SOLO-4) hears it during the solo race without answering.  After the
# solo RESULTS cab1 takes LOBBY, links with cab2 from there, and both run one
# linked race to the cap; cab1 takes EXIT (solo-then-link), cab2 takes EXIT
# (one-race), and both pass on the title and exit 0.
# It requires:
#   - both exit 0;
#   - cab1's report: "arcade link autopilot v3", "cab 1", "mode
#     solo-then-link", "result PASS (0)", "race ticks 1200", "freeze tick 0",
#     "desync tick 0", "peer heard RACING" (the first solo view with the
#     other cabinet heard was solo RACING), then exactly the race lines
#     "race 1 validated ...", "race 1 end reason FINISHED", "race 2 agreed
#     match ...", "race 2 validated ...", "race 2 end reason FINISHED" (race
#     1, the solo race, has no agreed match), and last "end races 2";
#   - cab2's report: "arcade link autopilot v3", "cab 2", "mode one-race",
#     "result PASS (0)", "race ticks 1200", "freeze tick 0", "desync tick 0",
#     exactly "race 1 agreed match ...", "race 1 validated ...", "race 1 end
#     reason FINISHED", no "peer heard" line, and last "end races 1";
#   - cab1's stdout: the autopilot's "solo-then-link mode" and "race tick
#     limit 1200" lines; exactly two race caller launch lines, the first solo
#     ("race <n> launched (track <t> laps <l>, solo)") and the second linked;
#     one RL-12 "race <n> validated" line per launch, in order, whose digests
#     equal the report's race 1 and race 2 lines; one drive end per launch
#     ("race tick limit" at race tick 1200, or "end of race" or "finish grace"
#     at or below it) and the per-tick digest lines of each launch contiguous
#     from race tick 0 to its drive end tick; two "ended (reason 1)" lines
#     (FINISHED); exactly one agreed match line, equal to the report's race 2
#     line and after the return to the LOBBY; no "out of sync" line; the
#     autopilot's "race 1 started (solo)", "solo RESULTS: LOBBY confirmed,
#     back in the LOBBY", "race 2 started", and "peer heard on the solo RACING
#     screen" lines once each, the last one after cab1's solo race tick seen
#     when cab2 was started and before the solo drive end; and its PASS line
#     (2 races started, 2 validated, 2 ended);
#   - cab2's stdout: the autopilot's "one-race mode" and "race tick limit
#     1200" lines; exactly one launch line, linked; one RL-12 validated line of
#     it equal to the report's; one drive end and contiguous per-tick lines;
#     one "ended (reason 1)" line; one agreed match line equal to the
#     report's; no "out of sync" line; its PASS line (1 race each);
#   - across the cabinets, cab1's race 2 against cab2's race 1: the same
#     agreed match, config, plan, bots, and bank digests, drive end kind and
#     tick, and per-tick V4 digest text on every race tick.
# It prints the wake margin: the race tick cab1's solo race had reached (at
# least) when cab2 was started, the race tick at which cab1 heard cab2, and
# the cap, so cab2's start-to-LOBBY time (at most the difference, at 30 race
# ticks a second) can be compared with the solo race's remaining length.
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
# The race tick cap on both runs, for both of cab1's races: a 1200-tick race
# (40 s) ends "race tick limit" unless it finishes first.  cab2 must reach its
# LOBBY within cab1's solo race: with 900 ticks the measured margin was 1.67x
# (cab2 started at solo race tick 9 or later, heard at race tick 542), under
# the wanted 2x, so the cap is 1200; see the CMake registration for the
# measured numbers.
$raceTickCap = 1200
# Race ticks per second (the race drive's 30 Hz pacing), for the margin line.
$raceTicksPerSecond = 30
$finishedEndKinds = @('end of race', 'finish grace', 'race tick limit')
$reportAgreedPattern = '^race ([0-9]+) (agreed match track [0-9]+ laps [0-9]+ seed 0x[0-9A-F]{16} slots( [0-9]+){8} \([12B-]{8}\))$'
$reportValidatedPattern = '^race ([0-9]+) validated launch ([0-9]+) config ([0-9a-f]{64}) plan ([0-9a-f]{64}) bots ([0-9a-f]{64}) bank ([0-9a-f]{64})$'
$stdoutSoloThenLinkModeLine = '[CTR Native] arcade link autopilot: solo-then-link mode'
$stdoutOneRaceModeLine = '[CTR Native] arcade link autopilot: one-race mode'
$stdoutRaceTickLimitLine = "[CTR Native] arcade link autopilot: race tick limit $raceTickCap"
$stdoutLaunchPattern = '^\[CTR Native\] arcade link: race ([0-9]+) launched \(track ([0-9]+) laps ([0-9]+)(, solo)?\)$'
$stdoutValidatedPattern = '^\[CTR Native\] arcade link: race ([0-9]+) validated config ([0-9a-f]{64}) plan ([0-9a-f]{64}) bots ([0-9a-f]{64}) bank ([0-9a-f]{64})$'
$stdoutDriveEndPattern = '^\[CTR Native\] arcade link: race ([0-9]+) drive end: (.*) at race tick ([0-9]+)$'
# LR-74's per-tick line: the digest text after "digests" is compared.
$stdoutTickPattern = '^\[CTR Native\] arcade link: race ([0-9]+) race tick ([0-9]+) digests (v4 .*)$'
$stdoutEndedPattern = '^\[CTR Native\] arcade link: race ([0-9]+) ended \(reason ([0-9]+)\); foreign bundles dropped ([0-9]+)$'
$stdoutAgreedPattern = '^\[CTR Native\] arcade link: (agreed match .*)$'
$stdoutOutOfSyncPattern = '^\[CTR Native\] arcade link: race ([0-9]+) out of sync '
$stdoutSoloStartedLine = '[CTR Native] arcade link autopilot: race 1 started (solo)'
$stdoutLobbyLine = '[CTR Native] arcade link autopilot: solo RESULTS: LOBBY confirmed, back in the LOBBY'
$stdoutLinkedStartedLine = '[CTR Native] arcade link autopilot: race 2 started'
$stdoutHeardLine = '[CTR Native] arcade link autopilot: peer heard on the solo RACING screen'
$stdoutHeardPrefix = '[CTR Native] arcade link autopilot: peer heard '
$stdoutCab1PassPattern = '^\[CTR Native\] arcade link autopilot: PASS after ([0-9]+) ticks \(2 races started, 2 validated, 2 ended; last screen [A-Z_]+ end reason [A-Z_]+\); exit code 0$'
$stdoutCab2PassPattern = '^\[CTR Native\] arcade link autopilot: PASS after ([0-9]+) ticks \(1 races started, 1 validated, 1 ended; last screen [A-Z_]+ end reason [A-Z_]+\); exit code 0$'
# cab2's start trigger: cab1's solo launch line, and the per-tick lines of
# that launch, over cab1's whole stdout text.
$triggerLaunchRegex = New-Object System.Text.RegularExpressions.Regex('^\[CTR Native\] arcade link: race ([0-9]+) launched \(track [0-9]+ laps [0-9]+, solo\)\r?$', [System.Text.RegularExpressions.RegexOptions]::Multiline)
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
        '--arcade-link-autopilot', $Run.ReportPath, $Run.ModeOption, '--arcade-link-autopilot-race-ticks', $raceTickCap)
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

function Write-ReportSummary($Run) {
    if (Test-Path -LiteralPath $Run.ReportPath -PathType Leaf) {
        foreach ($line in [System.IO.File]::ReadAllLines($Run.ReportPath)) {
            if ($line -match '^(cab|mode|result|last screen|ticks|race ticks|peer heard|race [0-9]+ end reason) ') {
                Write-Output "  $($Run.Name) report: $line"
            }
        }
    }
}

# The skip reason of an exited run, or $null.
function Get-SkipReason($Run) {
    $log = Get-RunLog $Run
    if ($log.Contains($noDisplayMarker)) {
        return "no display available to SDL (run $($Run.Name), see $($Run.StderrPath)); a desktop session is required"
    }
    if ($log.Contains($notInternalMarker)) {
        return "--arcade-link-autopilot was rejected: the executable is not an internal (CTR_INTERNAL) build (run $($Run.Name), see $($Run.StderrPath))"
    }
    if ($log.Contains($unknownIdentityMarker)) {
        return "the build identity is unknown (a build from a dirty tree), so the link cannot start (run $($Run.Name), see $($Run.StderrPath)); rebuild from a clean tree"
    }
    return $null
}

# Handles the runs that exited: a skip marker skips at once, a nonzero exit
# fails at once; either way every other run is stopped.
function Test-ExitedRuns($All) {
    foreach ($run in @($All | Where-Object { ($null -ne $_.Process) -and $_.Process.HasExited })) {
        $reason = Get-SkipReason $run
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
}

# The highest race tick of cab1's solo race on its stdout so far, or -1
# before its race tick 0 line; returns the solo launch number too.
function Get-SoloWatch($Run) {
    $text = Read-SharedText $Run.StdoutPath
    $launch = $triggerLaunchRegex.Match($text)
    if (-not $launch.Success) {
        return [pscustomobject]@{ Launch = 0; Tick = -1 }
    }
    $number = [int]$launch.Groups[1].Value
    $tickRegex = New-Object System.Text.RegularExpressions.Regex("^\[CTR Native\] arcade link: race $number race tick ([0-9]+) digests ", [System.Text.RegularExpressions.RegexOptions]::Multiline)
    $highest = -1
    foreach ($match in $tickRegex.Matches($text.Substring($launch.Index))) {
        $tick = [int]$match.Groups[1].Value
        if ($tick -gt $highest) {
            $highest = $tick
        }
    }
    return [pscustomobject]@{ Launch = $number; Tick = $highest }
}

# Starts cab1, then cab2 once cab1's solo race logged its race tick 0.
function Start-Runs($Cab1, $Cab2) {
    Start-Run $Cab1
    while ($true) {
        Test-ExitedRuns @($Cab1)
        if ($Cab1.Process.HasExited) {
            Exit-Failed "run $($Cab1.Name) exited 0 before its solo race started (see $($Cab1.StdoutPath) and $($Cab1.ReportPath))"
        }
        $watch = Get-SoloWatch $Cab1
        if ($watch.Tick -ge 0) {
            $Cab1.SoloLaunch = $watch.Launch
            $Cab1.TickAtPeerStart = $watch.Tick
            Start-Run $Cab2
            Write-Output ("started {0} {1} s after {2}, at {2}'s solo race (launch {3}) race tick {4} or later (the last on its stdout)" -f
                $Cab2.Name, [math]::Round(($Cab2.StartedAt - $Cab1.StartedAt).TotalSeconds, 1), $Cab1.Name, $watch.Launch, $watch.Tick)
            return
        }
        if ((Get-Date) -gt $deadline) {
            Stop-StartedRuns @($Cab1)
            Exit-Failed "timed out after $TimeoutSeconds s waiting for $($Cab1.Name)'s solo race tick 0 (logs in $resolvedOutput)"
        }
        Start-Sleep -Milliseconds 250
    }
}

# Waits for both runs.
function Wait-Runs($All) {
    while ($true) {
        Test-ExitedRuns $All
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

# The report of one run against its expected header and race lines (the
# exact sequence of "race <k> ..." lines, agreed and validated lines matched
# by pattern).  Returns the agreed texts and validated digests by race.
function Read-Report($Run, $ExpectedHeader, $ExpectedRaceLines, [int]$Races, $Failures) {
    $result = [pscustomobject]@{ Agreed = @{}; Validated = @{} }
    if (-not (Test-Path -LiteralPath $Run.ReportPath -PathType Leaf)) {
        [void]$Failures.Add("run $($Run.Name) wrote no report $($Run.ReportPath)")
        return $result
    }
    $lines = @([System.IO.File]::ReadAllLines($Run.ReportPath))
    for ($i = 0; $i -lt $ExpectedHeader.Count; $i++) {
        $found = ''
        if ($i -lt $lines.Count) {
            $found = $lines[$i]
        }
        if ($found -ne $ExpectedHeader[$i]) {
            [void]$Failures.Add("report $($Run.Name): line $($i + 1) is '$found', expected '$($ExpectedHeader[$i])'")
        }
    }
    foreach ($required in @("race ticks $raceTickCap", 'freeze tick 0', 'desync tick 0')) {
        $count = @($lines | Where-Object { $_ -ceq $required }).Count
        if ($count -ne 1) {
            [void]$Failures.Add("report $($Run.Name): $count '$required' lines, expected 1")
        }
    }
    $raceLines = @($lines | Where-Object { ($_ -match '^race ') -and ($_ -notmatch '^race ticks ') })
    $shapes = @()
    foreach ($line in $raceLines) {
        if ($line -match $reportAgreedPattern) {
            $result.Agreed[[int]$Matches[1]] = $Matches[2]
            $shapes += "race $($Matches[1]) agreed"
        }
        elseif ($line -match $reportValidatedPattern) {
            $result.Validated[[int]$Matches[1]] = [pscustomobject]@{ Launch = [int]$Matches[2]; Config = $Matches[3]; Plan = $Matches[4]; Bots = $Matches[5]; Bank = $Matches[6] }
            $shapes += "race $($Matches[1]) validated"
        }
        else {
            $shapes += $line
        }
    }
    if (($shapes -join ' | ') -cne ($ExpectedRaceLines -join ' | ')) {
        [void]$Failures.Add("report $($Run.Name): the race lines read '$($shapes -join ' | ')', expected '$($ExpectedRaceLines -join ' | ')'")
    }
    if (($lines.Count -eq 0) -or ($lines[$lines.Count - 1] -ne "end races $Races")) {
        [void]$Failures.Add("report $($Run.Name): the report does not end with 'end races $Races'")
    }
    return $result
}

# One pass over a run's stdout: every line the checks read, with its line
# index (the order of the process's own log).
function Read-Stdout($Run) {
    $log = [pscustomobject]@{
        Lines = @(); Launches = @(); Validated = @(); DriveEnds = @(); Ticks = @{}; Ended = @(); Agreed = @(); OutOfSync = @()
    }
    $index = 0
    $log.Lines = @((Read-SharedText $Run.StdoutPath) -split "`r?`n")
    foreach ($line in $log.Lines) {
        if ($line -match $stdoutTickPattern) {
            $launch = [int]$Matches[1]
            if (-not $log.Ticks.ContainsKey($launch)) {
                $log.Ticks[$launch] = New-Object System.Collections.ArrayList
            }
            [void]$log.Ticks[$launch].Add([pscustomobject]@{ Index = $index; Tick = [int]$Matches[2]; Digests = $Matches[3] })
        }
        elseif ($line -match $stdoutLaunchPattern) {
            $log.Launches += [pscustomobject]@{ Index = $index; Launch = [int]$Matches[1]; Track = $Matches[2]; Laps = $Matches[3]; Solo = ($Matches[4] -eq ', solo') }
        }
        elseif ($line -match $stdoutValidatedPattern) {
            $log.Validated += [pscustomobject]@{ Index = $index; Launch = [int]$Matches[1]; Config = $Matches[2]; Plan = $Matches[3]; Bots = $Matches[4]; Bank = $Matches[5] }
        }
        elseif ($line -match $stdoutDriveEndPattern) {
            $log.DriveEnds += [pscustomobject]@{ Index = $index; Launch = [int]$Matches[1]; Kind = $Matches[2]; Tick = [int]$Matches[3] }
        }
        elseif ($line -match $stdoutEndedPattern) {
            $log.Ended += [pscustomobject]@{ Index = $index; Reason = [int]$Matches[2]; Line = $line }
        }
        elseif ($line -match $stdoutAgreedPattern) {
            $log.Agreed += [pscustomobject]@{ Index = $index; Text = $Matches[1] }
        }
        elseif ($line -match $stdoutOutOfSyncPattern) {
            $log.OutOfSync += $line
        }
        $index++
    }
    return $log
}

# The index of the one line equal to $Text (-1 with a failure otherwise).
function Find-OnceLine($Run, $Log, [string]$Text, $Failures) {
    $found = @()
    for ($i = 0; $i -lt $Log.Lines.Count; $i++) {
        if ($Log.Lines[$i] -ceq $Text) {
            $found += $i
        }
    }
    if ($found.Count -ne 1) {
        [void]$Failures.Add("run $($Run.Name): $($found.Count) '$Text' lines on stdout, expected 1")
        return -1
    }
    return $found[0]
}

# The races of one run's stdout, in launch order: each launch's solo flag,
# validated digests, drive end, and per-tick digests (contiguous from race
# tick 0 to the drive end tick).  $ExpectSolo lists the expected solo flag of
# each race.
function Read-Races($Run, $Log, $ExpectSolo, $Failures) {
    $prefix = "run $($Run.Name)"
    $races = @()
    if ($Log.Launches.Count -ne $ExpectSolo.Count) {
        [void]$Failures.Add("${prefix}: $($Log.Launches.Count) race launch lines on stdout, expected $($ExpectSolo.Count)")
        return $races
    }
    if ($Log.Validated.Count -ne $ExpectSolo.Count) {
        [void]$Failures.Add("${prefix}: $($Log.Validated.Count) RL-12 'race <n> validated' lines on stdout, expected $($ExpectSolo.Count)")
        return $races
    }
    if ($Log.DriveEnds.Count -ne $ExpectSolo.Count) {
        [void]$Failures.Add("${prefix}: $($Log.DriveEnds.Count) drive end lines on stdout, expected $($ExpectSolo.Count)")
        return $races
    }
    $known = @($Log.Launches | ForEach-Object { $_.Launch })
    foreach ($launch in $Log.Ticks.Keys) {
        if ($known -notcontains $launch) {
            [void]$Failures.Add("${prefix}: per-tick digest lines of launch $launch, which is none of its races")
        }
    }
    for ($k = 0; $k -lt $ExpectSolo.Count; $k++) {
        $launch = $Log.Launches[$k]
        $number = $k + 1
        if ($launch.Solo -ne $ExpectSolo[$k]) {
            [void]$Failures.Add("${prefix}: race $number (launch $($launch.Launch)) solo is $($launch.Solo), expected $($ExpectSolo[$k])")
        }
        $validated = $Log.Validated[$k]
        $end = $Log.DriveEnds[$k]
        if (($validated.Launch -ne $launch.Launch) -or ($end.Launch -ne $launch.Launch)) {
            [void]$Failures.Add("${prefix}: race $number is launch $($launch.Launch), but its validated line is launch $($validated.Launch) and its drive end launch $($end.Launch)")
        }
        if ($finishedEndKinds -notcontains $end.Kind) {
            [void]$Failures.Add("${prefix}: race $number drive end '$($end.Kind)', expected one of $($finishedEndKinds -join ', ')")
        }
        elseif ((($end.Kind -eq 'race tick limit') -and ($end.Tick -ne $raceTickCap)) -or ($end.Tick -gt $raceTickCap)) {
            [void]$Failures.Add("${prefix}: race $number drive end '$($end.Kind)' at race tick $($end.Tick) (cap $raceTickCap)")
        }
        $ticks = @()
        if ($Log.Ticks.ContainsKey($launch.Launch)) {
            $ticks = @($Log.Ticks[$launch.Launch])
        }
        $digests = @{}
        $contiguous = ($ticks.Count -eq ($end.Tick + 1))
        for ($i = 0; ($i -lt $ticks.Count) -and $contiguous; $i++) {
            if ($ticks[$i].Tick -ne $i) {
                $contiguous = $false
            }
            $digests[$i] = $ticks[$i].Digests
        }
        if (-not $contiguous) {
            [void]$Failures.Add("${prefix}: race $number (launch $($launch.Launch)) has $($ticks.Count) per-tick digest lines, expected race ticks 0..$($end.Tick) in order (its drive end tick)")
        }
        $races += [pscustomobject]@{
            Launch = $launch.Launch; LaunchIndex = $launch.Index; Solo = $launch.Solo; Validated = $validated; End = $end
            Ticks = $ticks; Digests = $digests; Contiguous = $contiguous
        }
    }
    $endedOk = @($Log.Ended | Where-Object { $_.Reason -eq 1 })
    if (($Log.Ended.Count -ne $ExpectSolo.Count) -or ($endedOk.Count -ne $ExpectSolo.Count)) {
        [void]$Failures.Add("${prefix}: the 'ended (reason r)' lines on stdout are '$(@($Log.Ended | ForEach-Object { $_.Line }) -join ''', ''')', expected $($ExpectSolo.Count) with reason 1 (FINISHED)")
    }
    foreach ($line in $Log.OutOfSync) {
        [void]$Failures.Add("${prefix}: an out of sync line: '$line'")
    }
    return $races
}

# A run's validated stdout line against its report's line of race $K.
function Test-ValidatedEqual($Run, [int]$K, $Stdout, $Report, $Failures) {
    if (($null -eq $Stdout) -or ($null -eq $Report)) {
        return
    }
    foreach ($field in @('Launch', 'Config', 'Plan', 'Bots', 'Bank')) {
        if ($Stdout.$field -ne $Report.$field) {
            [void]$Failures.Add("run $($Run.Name): race $K $($field.ToLowerInvariant()) differs between stdout ($($Stdout.$field)) and the report ($($Report.$field))")
        }
    }
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
        @{ Name = 'cab1'; Cab = 'cab1'; CabNumber = 1; Port = '7401'; Peer = '127.0.0.1:7402'; ModeOption = '--arcade-link-autopilot-solo-then-link' },
        @{ Name = 'cab2'; Cab = 'cab2'; CabNumber = 2; Port = '7402'; Peer = '127.0.0.1:7401'; ModeOption = '--arcade-link-autopilot-one-race' })
    foreach ($spec in $specs) {
        $run = [pscustomobject]@{
            Name = $spec.Name
            Cab = $spec.Cab
            CabNumber = $spec.CabNumber
            Port = $spec.Port
            Peer = $spec.Peer
            ModeOption = $spec.ModeOption
            ReportPath = Join-Path $resolvedOutput "$($spec.Name).report.txt"
            StdoutPath = Join-Path $resolvedOutput "$($spec.Name).stdout.log"
            StderrPath = Join-Path $resolvedOutput "$($spec.Name).stderr.log"
            Process = $null
            StartedAt = $null
            SoloLaunch = 0
            TickAtPeerStart = -1
        }
        # A stale report from an earlier run must never pass for this one.
        foreach ($stale in @($run.ReportPath, $run.StdoutPath, $run.StderrPath)) {
            if (Test-Path -LiteralPath $stale) {
                Remove-Item -LiteralPath $stale -Force -ErrorAction Stop
            }
        }
        $runs += $run
    }
    $cab1 = $runs[0]
    $cab2 = $runs[1]

    Write-Output "arcade solo wake link check: cab1 (port 7401, peer 7402, solo-then-link) alone, then cab2 (port 7402, peer 7401, one-race) once cab1's solo race runs; race tick cap $raceTickCap"
    Write-Output "executable: $resolvedExecutable"
    Write-Output "output:     $resolvedOutput"

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    Start-Runs $cab1 $cab2
    Wait-Runs $runs

    $failures = New-Object System.Collections.ArrayList
    foreach ($run in $runs) {
        Write-Output ("run {0} exit {1} in {2} s" -f $run.Name, $run.Process.ExitCode, [math]::Round(($run.Process.ExitTime - $run.StartedAt).TotalSeconds, 1))
    }

    # The reports.
    $report1 = Read-Report $cab1 @('arcade link autopilot v3', 'cab 1', 'mode solo-then-link', 'result PASS (0)') `
        @('race 1 validated', 'race 1 end reason FINISHED', 'race 2 agreed', 'race 2 validated', 'race 2 end reason FINISHED') 2 $failures
    $report2 = Read-Report $cab2 @('arcade link autopilot v3', 'cab 2', 'mode one-race', 'result PASS (0)') `
        @('race 1 agreed', 'race 1 validated', 'race 1 end reason FINISHED') 1 $failures
    if (Test-Path -LiteralPath $cab1.ReportPath -PathType Leaf) {
        $heard = @([System.IO.File]::ReadAllLines($cab1.ReportPath) | Where-Object { $_ -match '^peer heard ' })
        if (($heard.Count -ne 1) -or ($heard[0] -cne 'peer heard RACING')) {
            [void]$failures.Add("report cab1: the 'peer heard' lines are '$($heard -join ''', ''')', expected exactly 'peer heard RACING' (cab2 heard during the solo race)")
        }
    }
    if (Test-Path -LiteralPath $cab2.ReportPath -PathType Leaf) {
        if (@([System.IO.File]::ReadAllLines($cab2.ReportPath) | Where-Object { $_ -match '^peer heard ' }).Count -ne 0) {
            [void]$failures.Add("report cab2: a 'peer heard' line in a one-race report")
        }
    }

    # cab1's stdout: the solo race, the LOBBY, the linked race.
    $log1 = Read-Stdout $cab1
    foreach ($required in @($stdoutSoloThenLinkModeLine, $stdoutRaceTickLimitLine)) {
        $null = Find-OnceLine $cab1 $log1 $required $failures
    }
    $soloStartedAt = Find-OnceLine $cab1 $log1 $stdoutSoloStartedLine $failures
    $lobbyAt = Find-OnceLine $cab1 $log1 $stdoutLobbyLine $failures
    $linkedStartedAt = Find-OnceLine $cab1 $log1 $stdoutLinkedStartedLine $failures
    $heardLines = @($log1.Lines | Where-Object { $_.StartsWith($stdoutHeardPrefix) })
    $heardAt = -1
    if (($heardLines.Count -ne 1) -or ($heardLines[0] -cne $stdoutHeardLine)) {
        [void]$failures.Add("run cab1: the 'peer heard' lines on stdout are '$($heardLines -join ''', ''')', expected exactly '$stdoutHeardLine'")
    }
    else {
        $heardAt = [array]::IndexOf($log1.Lines, $stdoutHeardLine)
    }
    if (@($log1.Lines | Where-Object { $_ -match $stdoutCab1PassPattern }).Count -ne 1) {
        [void]$failures.Add("run cab1: no autopilot PASS line (2 races started, validated, and ended) on stdout")
    }
    $races1 = @(Read-Races $cab1 $log1 @($true, $false) $failures)
    if ($log1.Agreed.Count -ne 1) {
        [void]$failures.Add("run cab1: $($log1.Agreed.Count) agreed match lines on stdout, expected 1 (its linked race 2 alone)")
    }
    elseif ($report1.Agreed.ContainsKey(2) -and ($log1.Agreed[0].Text -cne $report1.Agreed[2])) {
        [void]$failures.Add("run cab1: the agreed match differs between stdout ('$($log1.Agreed[0].Text)') and the report's race 2 ('$($report1.Agreed[2])')")
    }
    if ($races1.Count -eq 2) {
        Test-ValidatedEqual $cab1 1 $races1[0].Validated $report1.Validated[1] $failures
        Test-ValidatedEqual $cab1 2 $races1[1].Validated $report1.Validated[2] $failures
        if ($races1[0].Launch -ne $cab1.SoloLaunch) {
            [void]$failures.Add("run cab1: its first race is launch $($races1[0].Launch), but cab2 was started on solo launch $($cab1.SoloLaunch)")
        }
        # The order: the solo race and its drive end, the return to the
        # LOBBY, then the linked start (the autopilot's line on the
        # START_RACE tick), the hook's agreed match of that tick, and the
        # race caller's launch.
        $order = @(
            @('solo started', $soloStartedAt), @('solo drive end', $races1[0].End.Index), @('return to the LOBBY', $lobbyAt),
            @('race 2 started', $linkedStartedAt),
            @('agreed match', $(if ($log1.Agreed.Count -eq 1) { $log1.Agreed[0].Index } else { -1 })),
            @('linked launch', $races1[1].LaunchIndex))
        for ($i = 1; $i -lt $order.Count; $i++) {
            if (($order[$i - 1][1] -ge 0) -and ($order[$i][1] -ge 0) -and ($order[$i - 1][1] -ge $order[$i][1])) {
                [void]$failures.Add("run cab1: '$($order[$i - 1][0])' (stdout line $($order[$i - 1][1] + 1)) does not precede '$($order[$i][0])' (line $($order[$i][1] + 1))")
            }
        }
    }

    # The wake: heard during the solo race, after cab2 was started.
    $heardTick = -1
    if (($races1.Count -eq 2) -and ($heardAt -ge 0)) {
        $solo = $races1[0]
        $before = @($solo.Ticks | Where-Object { $_.Index -lt $heardAt })
        if (($before.Count -eq 0) -or ($heardAt -gt $solo.End.Index)) {
            [void]$failures.Add("run cab1: the 'peer heard' line (stdout line $($heardAt + 1)) is not inside its solo race (race ticks 0..$($solo.End.Tick))")
        }
        else {
            $heardTick = $before[$before.Count - 1].Tick
            if ($heardTick -lt $cab1.TickAtPeerStart) {
                [void]$failures.Add("run cab1: cab2 was heard at solo race tick $heardTick, before cab2 was started (race tick $($cab1.TickAtPeerStart) or later)")
            }
            else {
                $wakeTicks = $heardTick - $cab1.TickAtPeerStart
                $remaining = $solo.End.Tick - $cab1.TickAtPeerStart
                $margin = 'n/a'
                if ($wakeTicks -gt 0) {
                    $margin = '{0:N2}' -f ($remaining / $wakeTicks)
                }
                Write-Output ("wake: cab2 started at cab1's solo race tick {0} or later, heard at race tick {1}: cab2 start to LOBBY at most {2} race ticks ({3:N1} s); the solo race ran to race tick {4} ({5} race ticks, {6:N1} s, after cab2's start): margin {7}x" -f
                    $cab1.TickAtPeerStart, $heardTick, $wakeTicks, ($wakeTicks / $raceTicksPerSecond), $solo.End.Tick, $remaining, ($remaining / $raceTicksPerSecond), $margin)
            }
        }
    }

    # cab2's stdout: one linked race.
    $log2 = Read-Stdout $cab2
    foreach ($required in @($stdoutOneRaceModeLine, $stdoutRaceTickLimitLine)) {
        $null = Find-OnceLine $cab2 $log2 $required $failures
    }
    if (@($log2.Lines | Where-Object { $_ -match $stdoutCab2PassPattern }).Count -ne 1) {
        [void]$failures.Add("run cab2: no autopilot PASS line (1 race started, validated, and ended) on stdout")
    }
    if (@($log2.Lines | Where-Object { $_.StartsWith($stdoutHeardPrefix) }).Count -ne 0) {
        [void]$failures.Add("run cab2: a 'peer heard' line on the stdout of a one-race run")
    }
    $races2 = @(Read-Races $cab2 $log2 @($false) $failures)
    if ($log2.Agreed.Count -ne 1) {
        [void]$failures.Add("run cab2: $($log2.Agreed.Count) agreed match lines on stdout, expected 1")
    }
    elseif ($report2.Agreed.ContainsKey(1) -and ($log2.Agreed[0].Text -cne $report2.Agreed[1])) {
        [void]$failures.Add("run cab2: the agreed match differs between stdout ('$($log2.Agreed[0].Text)') and the report ('$($report2.Agreed[1])')")
    }
    if ($races2.Count -eq 1) {
        Test-ValidatedEqual $cab2 1 $races2[0].Validated $report2.Validated[1] $failures
    }

    # Across the cabinets: cab1's race 2 is cab2's race 1.
    if ($report1.Agreed.ContainsKey(2) -and $report2.Agreed.ContainsKey(1)) {
        if ($report1.Agreed[2] -cne $report2.Agreed[1]) {
            [void]$failures.Add("the agreed match differs: cab1 race 2 '$($report1.Agreed[2])', cab2 race 1 '$($report2.Agreed[1])'")
        }
        else {
            Write-Output "linked race (cab1 race 2, cab2 race 1): $($report1.Agreed[2])"
        }
    }
    if (($races1.Count -eq 2) -and ($races2.Count -eq 1)) {
        $linked1 = $races1[1]
        $linked2 = $races2[0]
        foreach ($field in @('Config', 'Plan', 'Bots', 'Bank')) {
            if ($linked1.Validated.$field -ne $linked2.Validated.$field) {
                [void]$failures.Add("the linked race's $($field.ToLowerInvariant()) digest differs: cab1 $($linked1.Validated.$field), cab2 $($linked2.Validated.$field)")
            }
        }
        if ($races1[0].Validated.Config -eq $linked1.Validated.Config) {
            [void]$failures.Add("cab1: the linked race's config digest equals the solo race's ($($linked1.Validated.Config)); the link did not go through the linked select")
        }
        if (($linked1.End.Kind -ne $linked2.End.Kind) -or ($linked1.End.Tick -ne $linked2.End.Tick)) {
            [void]$failures.Add("the linked race's drive end differs: cab1 '$($linked1.End.Kind)' at race tick $($linked1.End.Tick), cab2 '$($linked2.End.Kind)' at race tick $($linked2.End.Tick)")
        }
        if ($linked1.Contiguous -and $linked2.Contiguous) {
            $differing = @()
            for ($t = 0; $t -lt [math]::Min($linked1.Ticks.Count, $linked2.Ticks.Count); $t++) {
                if ($linked1.Digests[$t] -cne $linked2.Digests[$t]) {
                    $differing += $t
                }
            }
            if ($differing.Count -ne 0) {
                [void]$failures.Add("the linked race's per-tick digests differ on $($differing.Count) race tick(s), first race tick $($differing[0]): cab1 '$($linked1.Digests[$differing[0]])', cab2 '$($linked2.Digests[$differing[0]])'")
            }
            Write-Output ("linked race: drive end cab1 '{0}' at race tick {1}, cab2 '{2}' at race tick {3}; per-tick digest lines 0..{4} on both, all equal: {5}" -f
                $linked1.End.Kind, $linked1.End.Tick, $linked2.End.Kind, $linked2.End.Tick, ($linked1.Ticks.Count - 1), ($differing.Count -eq 0))
        }
        Write-Output ("cab1 solo race: launch {0}, drive end '{1}' at race tick {2}, {3} per-tick lines" -f $races1[0].Launch, $races1[0].End.Kind, $races1[0].End.Tick, $races1[0].Ticks.Count)
    }

    Write-Output ''
    if ($failures.Count -ne 0) {
        foreach ($failure in $failures) {
            Write-Output "FAIL: $failure"
        }
        foreach ($run in $runs) {
            Write-ReportSummary $run
        }
        Write-Output "arcade solo wake link check: FAIL ($($failures.Count) failure(s); reports and logs in $resolvedOutput)"
        Write-Output ("arcade solo wake link check: total time {0} s" -f [math]::Round(((Get-Date) - $checkStartedAt).TotalSeconds, 1))
        exit 1
    }
    Write-Output "cab1: one solo race (cab2 woke during it and was heard on solo RACING), LOBBY, then one linked race with cab2 on the same match, digests, and per-tick digests to FINISHED, EXIT; cab2: LOBBY, linked, one race, EXIT"
    Write-Output "arcade solo wake link check: PASS"
    Write-Output ("arcade solo wake link check: total time {0} s" -f [math]::Round(((Get-Date) - $checkStartedAt).TotalSeconds, 1))
    exit 0
}
finally {
    # On every exit and on an interrupt: no ctr_native this check started
    # outlives it.
    Stop-StartedRuns $runs
}
