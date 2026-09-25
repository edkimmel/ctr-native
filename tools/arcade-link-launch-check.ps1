[CmdletBinding()]
param(
    # Internal ctr_native build that accepts --arcade-link-autopilot.
    [Parameter(Mandatory = $true)]
    [string]$Executable,

    # Absolute directory for the reports, the race 1 frame captures, and the
    # stdout/stderr logs.  Both runs use it as their working directory; the
    # game itself still writes the gitignored `Crash Team Racing.log` in its
    # base directory.
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    # User-supplied disc image.  The check skips (exit 77) when it is absent.
    # Defaults to <repo>\assets\ctr-u.bin, resolved below: Windows PowerShell
    # 5.1 leaves $PSScriptRoot empty inside the param defaults of a
    # [CmdletBinding()] script run with -File.
    [string]$AssetsFile,

    # Seconds both runs together may take.
    [int]$TimeoutSeconds = 780
)

# Two-process live race gate (docs/RACE_LAUNCH_MILESTONE.md section 4 RL-15,
# slice RL-S10; since LR-S13 part B the one-machine proof of
# docs/LOCKSTEP_RACE_MILESTONE.md LR-16, LR-76).  Starts two internal
# ctr_native processes at once, linked over loopback:
#   cab1  --arcade-link cab1 --arcade-link-port 7001 --arcade-link-peer 127.0.0.1:7002
#   cab2  --arcade-link cab2 --arcade-link-port 7002 --arcade-link-peer 127.0.0.1:7001
# each with --arcade-link-autopilot <report>, which drives the link host's own
# inputs (never a pad) through START, the select, and three races in one run
# (include/platform/native_arcade_link_autopilot.h), each race on the linked
# race drive in lockstep with the autopilot's steering pad as the local
# sample, with --arcade-link-autopilot-race-ticks 6000 (LR-16's race-1 limit)
# and --capture-frame <captureFrame>=<output>\<cab>.race1.bmp (criterion 9);
# cab2 also gets --arcade-link-autopilot-freeze 600 and
# --arcade-link-autopilot-desync 300 (LR-73):
#   race 1, the finish: at race tick 600 cab2 freezes for 45 tick periods,
#     cab1 holds (with the banner) and resumes; both run to the natural finish
#     (or the finish grace) on the same race tick;
#   REMATCH; race 2, the desync: cab2 flips bit 0 of its recorded CONTROL
#     digest of race tick 300; at least one cabinet shows RACE OUT OF SYNC,
#     the other RACE OUT OF SYNC or OPPONENT DISCONNECTED (risk 7);
#   REMATCH; race 3, the peer drop: this check kills cab2 once its stdout
#     shows race 3's race tick 300 (stdout is fully buffered, so it is seen
#     in 4 KB chunks, a little late); cab1 stalls into OPPONENT DISCONNECTED
#     after 90 tick periods, takes EXIT, and exits 0 with PASS.
# It requires:
#   - cab1 exits 0 after the kill; cab2 runs until it is killed (its own exit,
#     before the kill, fails; its exit after the kill is expected);
#   - cab1's report is "arcade link autopilot v3" for cab 1 with "result PASS
#     (0)", "race ticks 6000", "freeze tick 0", "desync tick 0", and three
#     races, each with its agreed-match, validated, and end reason lines (end
#     reasons FINISHED, DESYNC or PEER_TIMEOUT, PEER_TIMEOUT), ending "end
#     races 3"; cab2 writes no report (killed): its evidence is its stdout;
#   - each stdout has three agreed-match lines, three "race <n> validated"
#     lines, and one "race tick limit 6000" line; the agreed match and the
#     config, plan, bots, and bank digests of the k-th race are equal across
#     the two processes (the k-th race of each, never equal launch numbers:
#     the race caller's launch number counts launch attempts on its own
#     cabinet, RL-S7 interpretation (d)); each race's config differs from the
#     previous race's (a new select); cab1's report agrees with its stdout
#     (agreed match, validated digests and launch numbers, end reasons);
#   - LR-8 (LR-S3 (b)): per race and cabinet exactly one "race <n> LR-8 race
#     tick <t> elapsedTimeMS <v> (timer <t+1>) ..." line for t = 0, 1, 2,
#     with the (elapsedTimeMS, rcntTotalUnits, clockFrameStart) triples of the
#     k-th race equal across the two, and exactly one setup pin readback line
#     "pinned rcntTotalUnits 0 clockFrameStart -200; read back 0 -200" before
#     each race's validated line, and no other;
#   - the per-tick digest lines (LR-74): per cabinet and race, one line per
#     race tick, contiguous from race tick 0 with no duplicate, up to the
#     race's drive end tick exactly (cab2's killed race 3: up to its last
#     flushed line); for every race tick both cabinets logged in the k-th race
#     the digest text is equal;
#   - race 1: one drive end per cabinet, "end of race" or "finish grace"
#     (recorded), equal kind and tick; both "ended (reason 1)"; no "out of
#     sync" line; cab2 has exactly one "race <n> race tick 600 froze 45 tick
#     periods" line (cab1 none); cab1 holds at a race tick in 600..602 for at
#     least 10 periods (the banner grace), and logs at least one "hold banner
#     presented as capture frame <n> in the game font" line in race 1 and no
#     "in the block font" line;
#   - race 2: cab2 logs the desync injection at its race 2's race tick 300
#     (cab1 none); at least one cabinet logs "race <m> out of sync at race
#     tick 300 domains 0x1 ..." (m the adapter's match count, the number of
#     its race 2 ended line) and ends race 2 with reason 3 (DESYNC); the other
#     ends it with reason 3 or 2 (PEER_TIMEOUT); both race 2 drive ends are
#     "outcome"; no out-of-sync line outside race 2;
#   - race 3: both cabinets validated it; cab1's drive end is "outcome", its
#     race 3 ended line reason 2, and its hold line at that end tick shows
#     the stall timeout: at least 90 tick periods (LR-44: the timeout is
#     counted on period 90, or on the period a late pump jumps to) in 90 to
#     112.5 periods of wall time (a 25% margin); cab2 has no race 3 drive end;
#     no "ended (reason 4)" (LINK ERROR) on either cabinet;
#   - each capture exists, starts with "BM", and is larger than a BMP header;
#     its "frame capture wrote <path>" line is on stdout once, right after a
#     race 1 per-tick line of a race tick below race 1's end tick (so the
#     captured frame is a race 1 frame; the margins are printed).
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
# main.c prints this one line both for an unknown build identity (a dirty-tree
# build) and for a missing content identity, so this skip covers both; the
# skip reason below names the build identity, the usual cause.
$unknownIdentityMarker = 'arcade link requires a known build and content identity.'
$races = 3
# LR-16's race-1 limit (LR-42, LR-60), on both cabinets: a race that reaches
# it ends "race tick limit", which race 1 does not accept; races 2 and 3 end
# earlier.
$raceTickCap = 6000
# cab2's fault injections (LR-73): race 1's freeze tick and race 2's desync
# tick, and the freeze's length (NATIVE_ARCADE_LINK_AUTOPILOT_FREEZE_PERIODS).
$freezeTick = 600
$desyncTick = 300
$freezePeriods = 45
# The input delay D (LR-3).  cab2 freezes on its race tick 600 before its race
# step, so the last bundle it sent is frame 599 + D; cab1 takes up to that
# frame and holds on race tick 600 + D at the latest (earlier only if cab1 was
# not yet there when cab2 froze and a bundle is late).
$inputDelay = 2
$holdBannerGracePeriods = 10
# Race 3's kill: cab2 is killed once its stdout shows this race tick of its
# third race.
$killRaceTick = 300
# The stall timeout (LR-12, NATIVE_ARCADE_NETPLAY_DEFAULT_STALL_TIMEOUT_TICKS)
# in tick periods of MAIN_ARCADE_RACE_HOLD_PERIOD_US, and the wall-time margin
# this check allows over it.
$stallTimeoutPeriods = 90
$tickPeriodUs = 33435
$stallMarginPercent = 25
# The frame both cabinets capture (criterion 9): frame N is the N-th present
# since the process started (include/platform/native_frame_capture.h).  It
# was chosen from measured runs to land well inside race 1 on both cabinets
# (docs/LOCKSTEP_RACE_MILESTONE.md LR-76); the check still requires it.
$captureFrame = 2800
$bmpHeaderBytes = 54
$expectedReportEnds = @('FINISHED', 'DESYNC|PEER_TIMEOUT', 'PEER_TIMEOUT')
$endReasonNames = @{ 1 = 'FINISHED'; 2 = 'PEER_TIMEOUT'; 3 = 'DESYNC'; 4 = 'LINK_ERROR'; 5 = 'OPPONENT_LEFT' }
$raceTicksPattern = '^race ticks ([0-9]+)$'
$freezeTickPattern = '^freeze tick ([0-9]+)$'
$desyncTickPattern = '^desync tick ([0-9]+)$'
$agreedPattern = '^race ([0-9]+) (agreed match track [0-9]+ laps [0-9]+ seed 0x[0-9A-F]{16} slots( [0-9]+){8} \([12B-]{8}\))$'
$validatedPattern = '^race ([0-9]+) validated launch ([0-9]+) config ([0-9a-f]{64}) plan ([0-9a-f]{64}) bots ([0-9a-f]{64}) bank ([0-9a-f]{64})$'
$endReasonPattern = '^race ([0-9]+) end reason ([A-Z_]+)$'
$stdoutRaceTickLimitPattern = '^\[CTR Native\] arcade link autopilot: race tick limit ([0-9]+)$'
$stdoutAgreedPattern = '^\[CTR Native\] arcade link: (agreed match .*)$'
$stdoutValidatedPattern = '^\[CTR Native\] arcade link: race ([0-9]+) validated config ([0-9a-f]{64}) plan ([0-9a-f]{64}) bots ([0-9a-f]{64}) bank ([0-9a-f]{64})$'
$stdoutElapsedPattern = '^\[CTR Native\] arcade link: race ([0-9]+) LR-8 race tick ([0-9]+) elapsedTimeMS (-?[0-9]+) \(timer (-?[0-9]+)\) rcntTotalUnits (-?[0-9]+) clockFrameStart (-?[0-9]+)$'
$stdoutPinPrefix = '[CTR Native] arcade race setup: pinned rcntTotalUnits '
$stdoutPinLine = '[CTR Native] arcade race setup: pinned rcntTotalUnits 0 clockFrameStart -200; read back 0 -200'
$stdoutDriveEndPattern = '^\[CTR Native\] arcade link: race ([0-9]+) drive end: (.*)$'
# The end text of the caller's drive end lines: the kind (with the failure
# reason for a local failure), then its race tick.
$driveEndTextPattern = '^(.*) at race tick ([0-9]+)( \(the drive did not run\))?$'
$raceOneEndKinds = @('end of race', 'finish grace')
# LR-74's per-tick line: the digest text after "digests" is compared.
$stdoutTickPattern = '^\[CTR Native\] arcade link: race ([0-9]+) race tick ([0-9]+) digests( v4 [0-9a-f]{16} v4control [0-9a-f]{16} v4rng [0-9a-f]{16} v4input [0-9a-f]{16} v4drivers [0-9a-f]{16} v4world [0-9a-f]{16} v4topology [0-9a-f]{16})$'
$stdoutEndedPattern = '^\[CTR Native\] arcade link: race ([0-9]+) ended \(reason ([0-9]+)\); foreign bundles dropped ([0-9]+)$'
$stdoutOutOfSyncPattern = '^\[CTR Native\] arcade link: race ([0-9]+) out of sync at race tick ([0-9]+) domains 0x([0-9a-f]+) local ([0-9a-f]{16}) remote ([0-9a-f]{16})$'
$stdoutHeldPattern = '^\[CTR Native\] arcade link: race ([0-9]+) race tick ([0-9]+) held ([0-9]+) tick periods \(([0-9]+) us\)$'
$stdoutFrozePattern = '^\[CTR Native\] arcade link: race ([0-9]+) race tick ([0-9]+) froze ([0-9]+) tick periods \(([0-9]+) us\)$'
$stdoutDesyncPattern = '^\[CTR Native\] arcade link: race ([0-9]+) race tick ([0-9]+): autopilot desync injection flipped bit 0 of the CONTROL domain digest$'
$stdoutBannerPattern = '^\[CTR Native\] hold banner presented as capture frame ([0-9]+)( in the (.*))?$'
$stdoutCapturePattern = '^\[CTR Native\] frame capture wrote (.*) \(([0-9]+)x([0-9]+)\)$'
# Race 3's kill watch: cab2's validated lines and per-tick lines, over its
# whole stdout text.
$killValidatedRegex = New-Object System.Text.RegularExpressions.Regex('^\[CTR Native\] arcade link: race ([0-9]+) validated config ', [System.Text.RegularExpressions.RegexOptions]::Multiline)
$elapsedTicks = 3
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
        '--arcade-link-autopilot', $Run.ReportPath, '--arcade-link-autopilot-race-ticks', $raceTickCap,
        '--capture-frame', "$captureFrame=$($Run.CapturePath)") + $Run.FaultArguments
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
            if ($line -match '^(result|last screen|ticks|race ticks|race [0-9]+ end reason) ') {
                Write-Output "  $($Run.Name) report: $line"
            }
        }
    }
}

# The highest race tick of the victim's third race on its stdout so far, or
# -1: the third race is the launch number of its third "race <n> validated"
# line.  Returns the launch number too.
function Get-KillWatch($Run) {
    $text = Read-SharedText $Run.StdoutPath
    $validated = $killValidatedRegex.Matches($text)
    if ($validated.Count -lt $races) {
        return [pscustomobject]@{ Launch = 0; Tick = -1 }
    }
    $launch = [int]$validated[$races - 1].Groups[1].Value
    $tickRegex = New-Object System.Text.RegularExpressions.Regex("^\[CTR Native\] arcade link: race $launch race tick ([0-9]+) digests v4 [0-9a-f]{16} ", [System.Text.RegularExpressions.RegexOptions]::Multiline)
    $highest = -1
    foreach ($match in $tickRegex.Matches($text.Substring($validated[$races - 1].Index))) {
        $tick = [int]$match.Groups[1].Value
        if ($tick -gt $highest) {
            $highest = $tick
        }
    }
    return [pscustomobject]@{ Launch = $launch; Tick = $highest }
}

# Waits for the runs, and kills the victim (cab2) in its third race.  A skip
# condition is known as soon as one run exits with its marker; the victim
# exiting before the kill, the survivor (cab1) exiting before the kill, or the
# survivor exiting nonzero fails at once, and the other run is stopped.
function Wait-Runs($Survivor, $Victim) {
    $all = @($Survivor, $Victim)
    while ($true) {
        foreach ($run in @($all | Where-Object { $_.Process.HasExited })) {
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
                Stop-StartedRuns $all
                Exit-Skipped $reason
            }
        }
        if ($Victim.Process.HasExited -and ($null -eq $Victim.KilledAt)) {
            $Victim.Process.WaitForExit()
            Stop-StartedRuns $all
            Write-ReportSummary $Victim
            Exit-Failed "run $($Victim.Name) exited $($Victim.Process.ExitCode) on its own before it was killed in its race 3 (see $($Victim.StdoutPath), $($Victim.StderrPath), and $($Victim.ReportPath))"
        }
        if ($Survivor.Process.HasExited) {
            $Survivor.Process.WaitForExit()
            if (($Survivor.Process.ExitCode -ne 0) -or ($null -eq $Victim.KilledAt)) {
                Stop-StartedRuns $all
                Write-ReportSummary $Survivor
                $when = 'after'
                if ($null -eq $Victim.KilledAt) {
                    $when = 'before'
                }
                Exit-Failed "run $($Survivor.Name) exited $($Survivor.Process.ExitCode) $when $($Victim.Name) was killed; the other run was stopped (see $($Survivor.StdoutPath), $($Survivor.StderrPath), and $($Survivor.ReportPath))"
            }
        }
        if ($null -eq $Victim.KilledAt) {
            $watch = Get-KillWatch $Victim
            if ($watch.Tick -ge $killRaceTick) {
                $killed = $false
                try {
                    $Victim.Process.Kill()
                    $killed = $true
                }
                catch {
                    # It exited between the check and the kill: the next pass
                    # reports it (KilledAt stays unset).
                }
                if ($killed) {
                    $Victim.Process.WaitForExit(10000) | Out-Null
                    $Victim.KilledAt = Get-Date
                    $Victim.KilledLaunch = $watch.Launch
                    $Victim.KilledSeenTick = $watch.Tick
                    Write-Output ("killed {0} at {1} s after its start, having seen its race 3 (launch {2}) race tick {3} on its stdout" -f
                        $Victim.Name, [math]::Round(($Victim.KilledAt - $Victim.StartedAt).TotalSeconds, 1), $watch.Launch, $watch.Tick)
                }
            }
        }
        if ($Survivor.Process.HasExited -and $Victim.Process.HasExited) {
            break
        }
        if ((Get-Date) -gt $deadline) {
            Stop-StartedRuns $all
            $pending = @($all | Where-Object { -not $_.Process.HasExited } | ForEach-Object { $_.Name })
            Exit-Failed "timed out after $TimeoutSeconds s waiting for run(s) $($pending -join ', ') (killed: $($null -ne $Victim.KilledAt); logs in $resolvedOutput)"
        }
        Start-Sleep -Milliseconds 500
    }
}

# Parses a report: the header, the agreed-match, validated, and end reason
# lines in file order, and the end line.  k is the line's own race number,
# which must run 1..$races in order.
function Read-Report($Run) {
    $lines = @([System.IO.File]::ReadAllLines($Run.ReportPath))
    $report = [pscustomobject]@{
        Lines = $lines
        Agreed = @()
        Validated = @()
        EndReasons = @()
        RaceTicks = @()
        FreezeTicks = @()
        DesyncTicks = @()
        Problems = @()
    }
    $expectedHeader = @('arcade link autopilot v3', "cab $($Run.CabNumber)", 'result PASS (0)')
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
        if ($line -match $raceTicksPattern) {
            $report.RaceTicks += [int]$Matches[1]
        }
        elseif ($line -match $freezeTickPattern) {
            $report.FreezeTicks += [int]$Matches[1]
        }
        elseif ($line -match $desyncTickPattern) {
            $report.DesyncTicks += [int]$Matches[1]
        }
        elseif ($line -match $agreedPattern) {
            $report.Agreed += [pscustomobject]@{ K = [int]$Matches[1]; Text = $Matches[2]; Line = $line }
        }
        elseif ($line -match $validatedPattern) {
            $report.Validated += [pscustomobject]@{
                K = [int]$Matches[1]; Launch = [int]$Matches[2]; Config = $Matches[3]; Plan = $Matches[4]; Bots = $Matches[5]
                Bank = $Matches[6]; Line = $line
            }
        }
        elseif ($line -match $endReasonPattern) {
            $report.EndReasons += [pscustomobject]@{ K = [int]$Matches[1]; Name = $Matches[2]; Line = $line }
        }
        elseif ($line -match '^race ') {
            $report.Problems += "malformed race line '$line'"
        }
    }
    foreach ($pair in @(@('race ticks', $report.RaceTicks, $raceTickCap), @('freeze tick', $report.FreezeTicks, 0), @('desync tick', $report.DesyncTicks, 0))) {
        if ($pair[1].Count -ne 1) {
            $report.Problems += "$($pair[1].Count) '$($pair[0])' lines, expected 1"
        }
        elseif ($pair[1][0] -ne $pair[2]) {
            $report.Problems += "'$($pair[0])' is $($pair[1][0]), expected $($pair[2]) (the value this check passed to $($Run.Name))"
        }
    }
    if ($report.Agreed.Count -ne $races) {
        $report.Problems += "$($report.Agreed.Count) agreed-match lines, expected $races"
    }
    if ($report.Validated.Count -ne $races) {
        $report.Problems += "$($report.Validated.Count) validated lines, expected $races (three races VALIDATED)"
    }
    if ($report.EndReasons.Count -ne $races) {
        $report.Problems += "$($report.EndReasons.Count) end reason lines, expected $races"
    }
    foreach ($list in @(@('agreed-match', $report.Agreed), @('validated', $report.Validated), @('end reason', $report.EndReasons))) {
        for ($k = 0; $k -lt $list[1].Count; $k++) {
            if ($list[1][$k].K -ne ($k + 1)) {
                $report.Problems += "$($list[0]) line $($k + 1) is numbered race $($list[1][$k].K)"
            }
        }
    }
    for ($k = 0; ($k -lt $report.EndReasons.Count) -and ($k -lt $races); $k++) {
        if ($report.EndReasons[$k].Name -notmatch "^($($expectedReportEnds[$k]))$") {
            $report.Problems += "race $($k + 1) end reason is $($report.EndReasons[$k].Name), expected $($expectedReportEnds[$k])"
        }
    }
    if (($lines.Count -eq 0) -or ($lines[$lines.Count - 1] -ne "end races $races")) {
        $report.Problems += "the report does not end with 'end races $races'"
    }
    return $report
}

# One pass over a run's stdout: every line the checks read, with its line
# index (the order of the process's own log).
function Read-Stdout($Run) {
    $log = [pscustomobject]@{
        Agreed = @(); Validated = @(); RaceTickLimits = @(); Ended = @(); OutOfSync = @(); DriveEnds = @(); Held = @(); Froze = @()
        Desyncs = @(); Banners = @(); Captures = @(); Ticks = @{}; LineCount = 0
    }
    $index = 0
    foreach ($line in ((Read-SharedText $Run.StdoutPath) -split "`r?`n")) {
        if ($line -match $stdoutTickPattern) {
            $launch = [int]$Matches[1]
            if (-not $log.Ticks.ContainsKey($launch)) {
                $log.Ticks[$launch] = New-Object System.Collections.ArrayList
            }
            [void]$log.Ticks[$launch].Add([pscustomobject]@{ Index = $index; Tick = [int]$Matches[2]; Digests = $Matches[3] })
        }
        elseif ($line -match $stdoutAgreedPattern) {
            $log.Agreed += [pscustomobject]@{ Index = $index; Text = $Matches[1] }
        }
        elseif ($line -match $stdoutValidatedPattern) {
            $log.Validated += [pscustomobject]@{ Index = $index; Launch = [int]$Matches[1]; Config = $Matches[2]; Plan = $Matches[3]; Bots = $Matches[4]; Bank = $Matches[5] }
        }
        elseif ($line -match $stdoutRaceTickLimitPattern) {
            $log.RaceTickLimits += [int]$Matches[1]
        }
        elseif ($line -match $stdoutEndedPattern) {
            $log.Ended += [pscustomobject]@{ Index = $index; Number = [int]$Matches[1]; Reason = [int]$Matches[2]; Line = $line }
        }
        elseif ($line -match $stdoutOutOfSyncPattern) {
            $log.OutOfSync += [pscustomobject]@{ Index = $index; Number = [int]$Matches[1]; Tick = [int]$Matches[2]; Mask = $Matches[3]; Line = $line }
        }
        elseif ($line -match $stdoutDriveEndPattern) {
            $log.DriveEnds += [pscustomobject]@{ Index = $index; Launch = [int]$Matches[1]; Text = $Matches[2] }
        }
        elseif ($line -match $stdoutHeldPattern) {
            $log.Held += [pscustomobject]@{ Index = $index; Launch = [int]$Matches[1]; Tick = [int]$Matches[2]; Periods = [int]$Matches[3]; Us = [long]$Matches[4] }
        }
        elseif ($line -match $stdoutFrozePattern) {
            $log.Froze += [pscustomobject]@{ Index = $index; Launch = [int]$Matches[1]; Tick = [int]$Matches[2]; Periods = [int]$Matches[3]; Us = [long]$Matches[4] }
        }
        elseif ($line -match $stdoutDesyncPattern) {
            $log.Desyncs += [pscustomobject]@{ Index = $index; Launch = [int]$Matches[1]; Tick = [int]$Matches[2] }
        }
        elseif ($line -match $stdoutBannerPattern) {
            $log.Banners += [pscustomobject]@{ Index = $index; Frame = [int]$Matches[1]; Font = $Matches[3] }
        }
        elseif ($line -match $stdoutCapturePattern) {
            $log.Captures += [pscustomobject]@{ Index = $index; Path = $Matches[1]; Width = [int]$Matches[2]; Height = [int]$Matches[3] }
        }
        $index++
    }
    $log.LineCount = $index
    return $log
}

# The line index range of the k-th race (0-based) on a stdout: from its
# validated line to the next race's validated line (or the end).
function Get-RaceSpan($Log, [int]$K) {
    $end = $Log.LineCount
    if (($K + 1) -lt $Log.Validated.Count) {
        $end = $Log.Validated[$K + 1].Index
    }
    return [pscustomobject]@{ Begin = $Log.Validated[$K].Index; End = $end }
}

function Select-InSpan($Items, $Span) {
    return @($Items | Where-Object { ($_.Index -gt $Span.Begin) -and ($_.Index -lt $Span.End) })
}

# LR-8: the elapsedTimeMS, rcntTotalUnits, and clockFrameStart of race ticks
# 0..2 of each race (its launch number from the validated lines), from the
# run's stdout.  Values[k] holds the k-th race's three
# "elapsedTimeMS/rcntTotalUnits/clockFrameStart" triples; each must come from
# exactly one line whose timer is the race tick + 1.  Each race must also
# have exactly one setup pin readback line (the LR-8 pin) before its
# validated line, and every pin readback line must be the pinned values.
function Read-ElapsedTimes($Run, $Log) {
    $result = [pscustomobject]@{ Values = @(); Problems = @() }
    $lines = @()
    $pinsBefore = @{}
    $pinsSince = 0
    foreach ($line in ((Read-SharedText $Run.StdoutPath) -split "`r?`n")) {
        if ($line -match $stdoutElapsedPattern) {
            $lines += [pscustomobject]@{ Launch = [int]$Matches[1]; Tick = [int]$Matches[2]; Elapsed = [int]$Matches[3]; Timer = [int]$Matches[4]; Units = [int]$Matches[5]; ClockFrameStart = [int]$Matches[6] }
        }
        elseif ($line.StartsWith($stdoutPinPrefix)) {
            if ($line -ne $stdoutPinLine) {
                $result.Problems += "run $($Run.Name): the setup's LR-8 pin readback is not the pinned values: '$line'"
            }
            $pinsSince++
        }
        elseif ($line -match $stdoutValidatedPattern) {
            $pinsBefore[[int]$Matches[1]] = $pinsSince
            $pinsSince = 0
        }
    }
    for ($k = 0; $k -lt $Log.Validated.Count; $k++) {
        $launch = $Log.Validated[$k].Launch
        $pins = 0
        if ($pinsBefore.ContainsKey($launch)) {
            $pins = $pinsBefore[$launch]
        }
        if ($pins -ne 1) {
            $result.Problems += "run $($Run.Name): race $($k + 1) (launch $launch) has $pins setup LR-8 pin readback lines before its validated line on stdout, expected 1 ('$stdoutPinLine')"
        }
    }
    for ($k = 0; $k -lt $Log.Validated.Count; $k++) {
        $launch = $Log.Validated[$k].Launch
        $values = @()
        for ($tick = 0; $tick -lt $elapsedTicks; $tick++) {
            $found = @($lines | Where-Object { ($_.Launch -eq $launch) -and ($_.Tick -eq $tick) })
            if ($found.Count -ne 1) {
                $result.Problems += "run $($Run.Name): race $($k + 1) (launch $launch) has $($found.Count) LR-8 elapsedTimeMS lines for race tick $tick on stdout, expected 1"
                $values += $null
            }
            elseif ($found[0].Timer -ne ($tick + 1)) {
                $result.Problems += "run $($Run.Name): race $($k + 1) (launch $launch) LR-8 race tick $tick was logged at timer $($found[0].Timer), expected $($tick + 1)"
                $values += $null
            }
            else {
                $values += "$($found[0].Elapsed)/$($found[0].Units)/$($found[0].ClockFrameStart)"
            }
        }
        $result.Values += , $values
    }
    return $result
}

# The drive's end of each race (its launch number): Values[k] is the k-th
# race's end (Kind, Tick, Text), or $null when it has none.  Every drive end
# line must belong to one of the races, and a race has at most one.
function Read-DriveEnds($Run, $Log) {
    $result = [pscustomobject]@{ Values = @(); Problems = @() }
    $launches = @($Log.Validated | ForEach-Object { $_.Launch })
    foreach ($line in $Log.DriveEnds) {
        if ($launches -notcontains $line.Launch) {
            $result.Problems += "run $($Run.Name): a drive end line of launch $($line.Launch), which is none of its races: '$($line.Text)'"
        }
    }
    for ($k = 0; $k -lt $Log.Validated.Count; $k++) {
        $launch = $Log.Validated[$k].Launch
        $found = @($Log.DriveEnds | Where-Object { $_.Launch -eq $launch })
        if ($found.Count -eq 0) {
            $result.Values += $null
            continue
        }
        if ($found.Count -ne 1) {
            $result.Problems += "run $($Run.Name): race $($k + 1) (launch $launch) has $($found.Count) drive end lines on stdout, expected at most 1"
        }
        if ($found[0].Text -notmatch $driveEndTextPattern) {
            $result.Problems += "run $($Run.Name): race $($k + 1) (launch $launch) drive end '$($found[0].Text)' does not read '<kind> at race tick <t>'"
            $result.Values += $null
            continue
        }
        $result.Values += [pscustomobject]@{ Kind = $Matches[1]; Tick = [int]$Matches[2]; Text = $found[0].Text; Index = $found[0].Index }
    }
    return $result
}

# The per-tick digest lines of the k-th race (LR-74): contiguous from race
# tick 0 with no duplicate, up to LastTick exactly when it is given (the
# drive end tick), else up to the last line logged.  Returns the digests by
# race tick, or $null with the problems added.
function Read-TickDigests($Run, $Log, [int]$K, $LastTick, $Problems) {
    $launch = $Log.Validated[$K].Launch
    $lines = @()
    if ($Log.Ticks.ContainsKey($launch)) {
        $lines = @($Log.Ticks[$launch])
    }
    if ($lines.Count -eq 0) {
        $Problems.Add("run $($Run.Name): race $($K + 1) (launch $launch) has no per-tick digest line") | Out-Null
        return $null
    }
    $digests = @{}
    for ($i = 0; $i -lt $lines.Count; $i++) {
        if ($lines[$i].Tick -ne $i) {
            $Problems.Add("run $($Run.Name): race $($K + 1) (launch $launch) per-tick digest line $($i + 1) is race tick $($lines[$i].Tick), expected $i (one line per race tick from 0, in order, no duplicate)") | Out-Null
            return $null
        }
        $digests[$i] = $lines[$i].Digests
    }
    if (($null -ne $LastTick) -and ($lines.Count - 1 -ne $LastTick)) {
        $Problems.Add("run $($Run.Name): race $($K + 1) (launch $launch) per-tick digest lines cover race ticks 0..$($lines.Count - 1), expected 0..$LastTick (its drive end tick)") | Out-Null
        return $null
    }
    return [pscustomobject]@{ Digests = $digests; Count = $lines.Count; First = $lines[0].Index; Last = $lines[$lines.Count - 1].Index }
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
        @{ Name = 'cab1'; Cab = 'cab1'; CabNumber = 1; Port = '7001'; Peer = '127.0.0.1:7002'; FaultArguments = @() },
        @{ Name = 'cab2'; Cab = 'cab2'; CabNumber = 2; Port = '7002'; Peer = '127.0.0.1:7001'
            FaultArguments = @('--arcade-link-autopilot-freeze', "$freezeTick", '--arcade-link-autopilot-desync', "$desyncTick") })
    foreach ($spec in $specs) {
        $run = [pscustomobject]@{
            Name = $spec.Name
            Cab = $spec.Cab
            CabNumber = $spec.CabNumber
            Port = $spec.Port
            Peer = $spec.Peer
            FaultArguments = $spec.FaultArguments
            ReportPath = Join-Path $resolvedOutput "$($spec.Name).report.txt"
            StdoutPath = Join-Path $resolvedOutput "$($spec.Name).stdout.log"
            StderrPath = Join-Path $resolvedOutput "$($spec.Name).stderr.log"
            # Absolute: a relative capture path resolves against the asset
            # base directory, not the working directory.
            CapturePath = Join-Path $resolvedOutput "$($spec.Name).race1.bmp"
            Process = $null
            StartedAt = $null
            KilledAt = $null
            KilledLaunch = 0
            KilledSeenTick = -1
        }
        # A stale report or capture from an earlier run must never pass for this one.
        foreach ($stale in @($run.ReportPath, $run.StdoutPath, $run.StderrPath, $run.CapturePath)) {
            if (Test-Path -LiteralPath $stale) {
                Remove-Item -LiteralPath $stale -Force -ErrorAction Stop
            }
        }
        $runs += $run
    }
    $cab1Run = $runs[0]
    $cab2Run = $runs[1]

    Write-Output "arcade link launch check: two linked runs over loopback (cab1 port 7001, cab2 port 7002), three races (LR-16): the finish, the desync, the peer drop"
    Write-Output "executable: $resolvedExecutable"
    Write-Output "output:     $resolvedOutput"
    Write-Output "race tick cap $raceTickCap on both; cab2: freeze at race 1 tick $freezeTick, desync at race 2 tick $desyncTick; cab2 killed at its race 3 tick $killRaceTick; capture frame $captureFrame on both"

    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    foreach ($run in $runs) {
        Start-Run $run
    }
    Wait-Runs $cab1Run $cab2Run

    $failures = New-Object System.Collections.ArrayList
    Write-Output ("run cab1 exit {0} in {1} s" -f $cab1Run.Process.ExitCode, [math]::Round(($cab1Run.Process.ExitTime - $cab1Run.StartedAt).TotalSeconds, 1))
    Write-Output ("run cab2 killed at {0} s (exit {1})" -f [math]::Round(($cab2Run.KilledAt - $cab2Run.StartedAt).TotalSeconds, 1), $cab2Run.Process.ExitCode)
    Write-Output ("both runs done in {0} s" -f [math]::Round(((Get-Date) - $checkStartedAt).TotalSeconds, 1))
    if (-not (Test-Path -LiteralPath $cab1Run.ReportPath -PathType Leaf)) {
        Exit-Failed "run cab1 wrote no report $($cab1Run.ReportPath)"
    }
    if (Test-Path -LiteralPath $cab2Run.ReportPath) {
        [void]$failures.Add("run cab2 wrote a report $($cab2Run.ReportPath), but it was killed in race 3")
    }

    $report = Read-Report $cab1Run
    foreach ($problem in $report.Problems) {
        [void]$failures.Add("report cab1: $problem")
    }
    $logs = @{}
    foreach ($run in $runs) {
        $log = Read-Stdout $run
        $logs[$run.Name] = $log
        if ($log.Agreed.Count -ne $races) {
            [void]$failures.Add("run $($run.Name): $($log.Agreed.Count) agreed-match lines on stdout, expected $races")
        }
        if ($log.Validated.Count -ne $races) {
            [void]$failures.Add("run $($run.Name): $($log.Validated.Count) 'race <n> validated' lines on stdout, expected $races (all three races VALIDATED)")
        }
        if (($log.RaceTickLimits.Count -ne 1) -or ($log.RaceTickLimits[0] -ne $raceTickCap)) {
            [void]$failures.Add("run $($run.Name): the race tick limit lines on stdout are '$($log.RaceTickLimits -join ', ')', expected one '$raceTickCap'")
        }
    }
    if ($failures.Count -ne 0) {
        foreach ($failure in $failures) {
            Write-Output "FAIL: $failure"
        }
        Write-Output "arcade link launch check: FAIL"
        exit 1
    }
    $cab1 = $logs['cab1']
    $cab2 = $logs['cab2']

    # cab1's report against its own stdout: the k-th agreed match, validated
    # line, and end reason.
    for ($k = 0; $k -lt $races; $k++) {
        if ($cab1.Agreed[$k].Text -ne $report.Agreed[$k].Text) {
            [void]$failures.Add("cab1: race $($k + 1) agreed match differs between stdout ('$($cab1.Agreed[$k].Text)') and the report ('$($report.Agreed[$k].Text)')")
        }
        foreach ($field in @('Launch', 'Config', 'Plan', 'Bots', 'Bank')) {
            if ($cab1.Validated[$k].$field -ne $report.Validated[$k].$field) {
                [void]$failures.Add("cab1: race $($k + 1) $($field.ToLowerInvariant()) differs between stdout ($($cab1.Validated[$k].$field)) and the report ($($report.Validated[$k].$field))")
            }
        }
        $stdoutReason = 'none'
        if ($k -lt $cab1.Ended.Count) {
            $stdoutReason = $endReasonNames[$cab1.Ended[$k].Reason]
        }
        if ($stdoutReason -ne $report.EndReasons[$k].Name) {
            [void]$failures.Add("cab1: race $($k + 1) end reason differs between stdout ($stdoutReason) and the report ($($report.EndReasons[$k].Name))")
        }
    }

    # LR-8 and the drive ends, per cabinet.
    $elapsed = @{}
    $driveEnds = @{}
    foreach ($run in $runs) {
        $log = $logs[$run.Name]
        $times = Read-ElapsedTimes $run $log
        foreach ($problem in $times.Problems) {
            [void]$failures.Add($problem)
        }
        $elapsed[$run.Name] = $times.Values
        $ends = Read-DriveEnds $run $log
        foreach ($problem in $ends.Problems) {
            [void]$failures.Add($problem)
        }
        $driveEnds[$run.Name] = $ends.Values
    }
    # Which races have a drive end: all three on cab1, races 1 and 2 on cab2
    # (killed in race 3).
    for ($k = 0; $k -lt $races; $k++) {
        foreach ($name in @('cab1', 'cab2')) {
            $expected = ($name -eq 'cab1') -or ($k -lt ($races - 1))
            $has = $null -ne $driveEnds[$name][$k]
            if ($expected -and -not $has) {
                [void]$failures.Add("run ${name}: race $($k + 1) (launch $($logs[$name].Validated[$k].Launch)) has no drive end line on stdout")
            }
            elseif ((-not $expected) -and $has) {
                [void]$failures.Add("run ${name}: race $($k + 1) has a drive end ('$($driveEnds[$name][$k].Text)'), but it was killed in that race")
            }
        }
    }
    # The ended lines (LR-36), paired by order: three on cab1, two on cab2.
    if ($cab1.Ended.Count -ne $races) {
        [void]$failures.Add("run cab1: $($cab1.Ended.Count) 'ended (reason r)' lines on stdout, expected $races")
    }
    if ($cab2.Ended.Count -ne ($races - 1)) {
        [void]$failures.Add("run cab2: $($cab2.Ended.Count) 'ended (reason r)' lines on stdout, expected $($races - 1) (it was killed in race 3)")
    }
    foreach ($name in @('cab1', 'cab2')) {
        foreach ($line in @($logs[$name].Ended | Where-Object { $_.Reason -eq 4 })) {
            [void]$failures.Add("run ${name}: LINK ERROR: '$($line.Line)'")
        }
        for ($k = 0; $k -lt [math]::Min($logs[$name].Ended.Count, $races); $k++) {
            $span = Get-RaceSpan $logs[$name] $k
            if (@(Select-InSpan @($logs[$name].Ended[$k]) $span).Count -ne 1) {
                [void]$failures.Add("run ${name}: its ended line $($k + 1) ('$($logs[$name].Ended[$k].Line)') is not in race $($k + 1)'s part of its stdout")
            }
        }
    }
    if ($failures.Count -ne 0) {
        foreach ($failure in $failures) {
            Write-Output "FAIL: $failure"
        }
        Write-Output "arcade link launch check: FAIL"
        exit 1
    }

    # The k-th race of cab1 against the k-th race of cab2: the agreed match,
    # the setup digests, LR-8, and the per-tick digests.
    for ($k = 0; $k -lt $races; $k++) {
        $number = $k + 1
        if ($cab1.Agreed[$k].Text -ne $cab2.Agreed[$k].Text) {
            [void]$failures.Add("race $number agreed match differs: cab1 '$($cab1.Agreed[$k].Text)', cab2 '$($cab2.Agreed[$k].Text)'")
        }
        foreach ($field in @('Config', 'Plan', 'Bots', 'Bank')) {
            if ($cab1.Validated[$k].$field -ne $cab2.Validated[$k].$field) {
                [void]$failures.Add("race $number $($field.ToLowerInvariant()) digest differs: cab1 $($cab1.Validated[$k].$field), cab2 $($cab2.Validated[$k].$field)")
            }
        }
        if ($k -gt 0) {
            foreach ($name in @('cab1', 'cab2')) {
                if ($logs[$name].Validated[$k].Config -eq $logs[$name].Validated[$k - 1].Config) {
                    [void]$failures.Add("${name}: race ${number}'s config digest equals race $($k)'s ($($logs[$name].Validated[$k].Config)); the rematch did not go through a new select")
                }
            }
        }
        Write-Output "race ${number}: cab1 $($report.Agreed[$k].Line)"
        Write-Output "race ${number}: cab2 agreed match: $($cab2.Agreed[$k].Text)"
        Write-Output "race ${number}: cab1 $($report.Validated[$k].Line)"
        Write-Output "race ${number}: cab2 validated launch $($cab2.Validated[$k].Launch) config $($cab2.Validated[$k].Config)"
        $elapsed1 = $elapsed['cab1'][$k] -join ' '
        $elapsed2 = $elapsed['cab2'][$k] -join ' '
        if ($elapsed1 -ne $elapsed2) {
            [void]$failures.Add("race $number LR-8 elapsedTimeMS/rcntTotalUnits/clockFrameStart of race ticks 0..2 differs: cab1 $elapsed1, cab2 $elapsed2")
        }
        Write-Output "race ${number}: LR-8 elapsedTimeMS/rcntTotalUnits/clockFrameStart race ticks 0..2: cab1 $elapsed1, cab2 $elapsed2"
    }

    $problems = New-Object System.Collections.ArrayList
    $tickDigests = @{}
    foreach ($name in @('cab1', 'cab2')) {
        $tickDigests[$name] = @()
        for ($k = 0; $k -lt $races; $k++) {
            $lastTick = $null
            if ($null -ne $driveEnds[$name][$k]) {
                $lastTick = $driveEnds[$name][$k].Tick
            }
            $tickDigests[$name] += , (Read-TickDigests (@($runs | Where-Object { $_.Name -eq $name })[0]) $logs[$name] $k $lastTick $problems)
        }
    }
    foreach ($problem in $problems) {
        [void]$failures.Add($problem)
    }
    for ($k = 0; $k -lt $races; $k++) {
        $one = $tickDigests['cab1'][$k]
        $two = $tickDigests['cab2'][$k]
        if (($null -eq $one) -or ($null -eq $two)) {
            continue
        }
        $common = [math]::Min($one.Count, $two.Count)
        $differing = @()
        for ($t = 0; $t -lt $common; $t++) {
            if ($one.Digests[$t] -ne $two.Digests[$t]) {
                $differing += $t
            }
        }
        if ($differing.Count -ne 0) {
            [void]$failures.Add("race $($k + 1): the per-tick digests differ across the cabinets on $($differing.Count) race tick(s), first race tick $($differing[0]): cab1 '$($one.Digests[$differing[0]])', cab2 '$($two.Digests[$differing[0]])'")
        }
        Write-Output ("race {0}: per-tick digest lines cab1 race ticks 0..{1}, cab2 0..{2}; the {3} common race ticks equal: {4}" -f ($k + 1), ($one.Count - 1), ($two.Count - 1), $common, ($differing.Count -eq 0))
    }

    # Race 1: the finish, the freeze, the hold, and the banner.
    $end1 = $driveEnds['cab1'][0]
    $end2 = $driveEnds['cab2'][0]
    if ($raceOneEndKinds -notcontains $end1.Kind) {
        [void]$failures.Add("race 1: cab1's drive end is '$($end1.Text)', expected 'end of race' or 'finish grace'")
    }
    if (($end1.Kind -ne $end2.Kind) -or ($end1.Tick -ne $end2.Tick)) {
        [void]$failures.Add("race 1 drive end differs: cab1 '$($end1.Text)', cab2 '$($end2.Text)'")
    }
    Write-Output "race 1: drive end: cab1 $($end1.Text), cab2 $($end2.Text)"
    foreach ($name in @('cab1', 'cab2')) {
        if ($logs[$name].Ended[0].Reason -ne 1) {
            [void]$failures.Add("race 1: ${name} ended with reason $($logs[$name].Ended[0].Reason), expected 1 (FINISHED)")
        }
    }
    Write-Output "race 1: ended: cab1 '$($cab1.Ended[0].Line)', cab2 '$($cab2.Ended[0].Line)'"
    $race1Span1 = Get-RaceSpan $cab1 0
    $race1Span2 = Get-RaceSpan $cab2 0
    $froze = @($cab2.Froze)
    if (($froze.Count -ne 1) -or (@(Select-InSpan $froze $race1Span2).Count -ne 1) -or ($froze[0].Launch -ne $cab2.Validated[0].Launch) -or
        ($froze[0].Tick -ne $freezeTick) -or ($froze[0].Periods -ne $freezePeriods)) {
        [void]$failures.Add("race 1: cab2's freeze lines are '$(($froze | ForEach-Object { "race $($_.Launch) race tick $($_.Tick) froze $($_.Periods)" }) -join '; ')', expected exactly one 'race $($cab2.Validated[0].Launch) race tick $freezeTick froze $freezePeriods tick periods' in its race 1")
    }
    if ($cab1.Froze.Count -ne 0) {
        [void]$failures.Add("cab1 logged $($cab1.Froze.Count) freeze line(s); only cab2 is given the freeze")
    }
    $race1Holds = @(Select-InSpan $cab1.Held $race1Span1 | Where-Object { $_.Launch -eq $cab1.Validated[0].Launch })
    $freezeHolds = @($race1Holds | Where-Object { ($_.Tick -ge $freezeTick) -and ($_.Tick -le ($freezeTick + $inputDelay)) -and ($_.Periods -ge $holdBannerGracePeriods) })
    if ($freezeHolds.Count -eq 0) {
        [void]$failures.Add("race 1: cab1 has no hold of at least $holdBannerGracePeriods tick periods at a race tick in $freezeTick..$($freezeTick + $inputDelay) (its race 1 holds: $(($race1Holds | ForEach-Object { "race tick $($_.Tick) held $($_.Periods)" }) -join '; '))")
    }
    else {
        Write-Output ("race 1: cab2 race tick {0} froze {1} tick periods ({2} us); cab1 race tick {3} held {4} tick periods ({5} us)" -f
            $froze[0].Tick, $froze[0].Periods, $froze[0].Us, $freezeHolds[0].Tick, $freezeHolds[0].Periods, $freezeHolds[0].Us)
    }
    $race1Banners = @(Select-InSpan $cab1.Banners $race1Span1)
    $gameFont = @($race1Banners | Where-Object { $_.Font -eq 'game font' })
    $blockFont = @($race1Banners | Where-Object { ($null -eq $_.Font) -or ($_.Font -ne 'game font') })
    if (($gameFont.Count -eq 0) -or ($blockFont.Count -ne 0)) {
        [void]$failures.Add("race 1: cab1 logged $($gameFont.Count) hold banner line(s) in the game font and $($blockFont.Count) other(s) (expected at least one, all in the game font)$(($blockFont | Select-Object -First 1 | ForEach-Object { ", first other: frame $($_.Frame) in the $($_.Font)" }))")
    }
    else {
        Write-Output ("race 1: cab1 presented {0} hold banner(s), all in the game font (capture frames {1}..{2})" -f $gameFont.Count, $gameFont[0].Frame, $gameFont[$gameFont.Count - 1].Frame)
    }

    # Out-of-sync lines: only race 2's, and only for race tick 300's CONTROL
    # domain.
    $detecting = @()
    foreach ($name in @('cab1', 'cab2')) {
        $log = $logs[$name]
        $race2Span = Get-RaceSpan $log 1
        foreach ($line in $log.OutOfSync) {
            if (@(Select-InSpan @($line) $race2Span).Count -ne 1) {
                [void]$failures.Add("run ${name}: an out-of-sync line outside race 2: '$($line.Line)'")
            }
            elseif (($line.Tick -ne $desyncTick) -or ($line.Mask -ne '1')) {
                [void]$failures.Add("run ${name}: race 2's out-of-sync line is '$($line.Line)', expected race tick $desyncTick domains 0x1")
            }
            elseif (($log.Ended.Count -ge 2) -and ($line.Number -eq $log.Ended[1].Number) -and ($log.Ended[1].Reason -eq 3)) {
                $detecting += $name
            }
        }
        if (($log.Ended.Count -ge 2) -and (@(2, 3) -notcontains $log.Ended[1].Reason)) {
            [void]$failures.Add("race 2: ${name} ended with reason $($log.Ended[1].Reason), expected 3 (DESYNC) or 2 (PEER_TIMEOUT)")
        }
    }

    # Race 2: the injection, the detection, and the ends.
    $injections = @($cab2.Desyncs)
    if (($injections.Count -ne 1) -or (@(Select-InSpan $injections (Get-RaceSpan $cab2 1)).Count -ne 1) -or
        ($injections[0].Launch -ne $cab2.Validated[1].Launch) -or ($injections[0].Tick -ne $desyncTick)) {
        [void]$failures.Add("race 2: cab2's desync injection lines are '$(($injections | ForEach-Object { "race $($_.Launch) race tick $($_.Tick)" }) -join '; ')', expected exactly one at race $($cab2.Validated[1].Launch) race tick $desyncTick in its race 2")
    }
    if ($cab1.Desyncs.Count -ne 0) {
        [void]$failures.Add("cab1 logged $($cab1.Desyncs.Count) desync injection line(s); only cab2 is given the injection")
    }
    if ($detecting.Count -eq 0) {
        [void]$failures.Add("race 2: no cabinet logged 'out of sync at race tick $desyncTick domains 0x1' for its race 2 and ended it with reason 3 (DESYNC)")
    }
    foreach ($name in @('cab1', 'cab2')) {
        $end = $driveEnds[$name][1]
        if (($null -ne $end) -and ($end.Kind -ne 'outcome')) {
            [void]$failures.Add("race 2: ${name}'s drive end is '$($end.Text)', expected 'outcome'")
        }
    }
    Write-Output ("race 2: cab2 flipped its CONTROL digest of race tick {0}; detected (out of sync, reason 3) by: {1}" -f $desyncTick, ($detecting -join ', '))
    foreach ($name in @('cab1', 'cab2')) {
        foreach ($line in $logs[$name].OutOfSync) {
            Write-Output "race 2: $name $($line.Line.Substring($line.Line.IndexOf('race ')))"
        }
        Write-Output "race 2: $name drive end $($driveEnds[$name][1].Text); $($logs[$name].Ended[1].Line.Substring($logs[$name].Ended[1].Line.IndexOf('race ')))"
    }

    # Race 3: the kill and cab1's stall timeout.
    $end3 = $driveEnds['cab1'][2]
    if ($null -ne $end3) {
        if ($end3.Kind -ne 'outcome') {
            [void]$failures.Add("race 3: cab1's drive end is '$($end3.Text)', expected 'outcome'")
        }
        if ($cab1.Ended[2].Reason -ne 2) {
            [void]$failures.Add("race 3: cab1 ended with reason $($cab1.Ended[2].Reason), expected 2 (PEER_TIMEOUT)")
        }
        $stall = @($cab1.Held | Where-Object { ($_.Launch -eq $cab1.Validated[2].Launch) -and ($_.Tick -eq $end3.Tick) })
        $minimumUs = [long]$stallTimeoutPeriods * $tickPeriodUs
        $maximumUs = [long]($minimumUs * (100 + $stallMarginPercent) / 100)
        if ($stall.Count -ne 1) {
            [void]$failures.Add("race 3: cab1 has $($stall.Count) hold lines at its end race tick $($end3.Tick), expected 1 (the stall timeout)")
        }
        elseif (($stall[0].Periods -lt $stallTimeoutPeriods) -or ($stall[0].Us -lt $minimumUs) -or ($stall[0].Us -gt $maximumUs)) {
            [void]$failures.Add("race 3: cab1's stall at race tick $($stall[0].Tick) held $($stall[0].Periods) tick periods in $($stall[0].Us) us, expected at least $stallTimeoutPeriods periods in $minimumUs..$maximumUs us")
        }
        else {
            Write-Output ("race 3: cab2 killed after its race tick {0} was seen; cab1 drive end {1}, race tick {2} held {3} tick periods ({4} us; the stall timeout is {5} periods, {6} us, margin {7}%); {8}" -f
                $cab2Run.KilledSeenTick, $end3.Text, $stall[0].Tick, $stall[0].Periods, $stall[0].Us, $stallTimeoutPeriods, $minimumUs, $stallMarginPercent,
                $cab1.Ended[2].Line.Substring($cab1.Ended[2].Line.IndexOf('race ')))
        }
    }
    if ($cab2Run.KilledLaunch -ne $cab2.Validated[2].Launch) {
        [void]$failures.Add("race 3: the kill watched launch $($cab2Run.KilledLaunch), but cab2's third race is launch $($cab2.Validated[2].Launch)")
    }

    # The captures (criterion 9): each a BMP, written in race 1.
    foreach ($run in $runs) {
        $log = $logs[$run.Name]
        $captures = @($log.Captures | Where-Object { $_.Path -eq $run.CapturePath })
        if (-not (Test-Path -LiteralPath $run.CapturePath -PathType Leaf)) {
            [void]$failures.Add("run $($run.Name): no capture $($run.CapturePath)")
            continue
        }
        [byte[]]$head = New-Object byte[] 2
        $stream = [System.IO.File]::OpenRead($run.CapturePath)
        try {
            $read = $stream.Read($head, 0, 2)
            $size = $stream.Length
        }
        finally {
            $stream.Dispose()
        }
        if (($read -ne 2) -or ($head[0] -ne 0x42) -or ($head[1] -ne 0x4D) -or ($size -le $bmpHeaderBytes)) {
            [void]$failures.Add("run $($run.Name): $($run.CapturePath) is not a BMP larger than its header ($size bytes)")
            continue
        }
        if (($log.Captures.Count -ne 1) -or ($captures.Count -ne 1)) {
            [void]$failures.Add("run $($run.Name): $($log.Captures.Count) 'frame capture wrote' lines on stdout, expected one for $($run.CapturePath)")
            continue
        }
        $digests = $tickDigests[$run.Name][0]
        $ticks = @($log.Ticks[$log.Validated[0].Launch] | Where-Object { $_.Index -lt $captures[0].Index })
        $endTick = $driveEnds[$run.Name][0].Tick
        if (($null -eq $digests) -or ($ticks.Count -eq 0) -or ($captures[0].Index -gt $digests.Last)) {
            [void]$failures.Add("run $($run.Name): its frame $captureFrame capture is not between race 1's first and last per-tick lines on stdout")
            continue
        }
        $captureTick = $ticks[$ticks.Count - 1].Tick
        if ($captureTick -ge $endTick) {
            [void]$failures.Add("run $($run.Name): its frame $captureFrame capture follows race 1's race tick $captureTick, not below its end tick $endTick")
            continue
        }
        Write-Output ("capture: {0} frame {1} ({2}x{3}, {4} bytes) {5}, after race 1's race tick {6} of {7} (margins {6} and {8} race ticks)" -f
            $run.Name, $captureFrame, $captures[0].Width, $captures[0].Height, $size, $run.CapturePath, $captureTick, $endTick, ($endTick - $captureTick))
    }

    Write-Output ''
    if ($failures.Count -ne 0) {
        foreach ($failure in $failures) {
            Write-Output "FAIL: $failure"
        }
        Write-Output "arcade link launch check: FAIL ($($failures.Count) failure(s); reports and logs in $resolvedOutput)"
        Write-Output ("arcade link launch check: total time {0} s" -f [math]::Round(((Get-Date) - $checkStartedAt).TotalSeconds, 1))
        exit 1
    }
    Write-Output "races 1..3: agreed match, config, plan, bots, and bank equal across cab1 and cab2 (paired by race order; launch numbers cab1 $(($cab1.Validated | ForEach-Object { $_.Launch }) -join ',') cab2 $(($cab2.Validated | ForEach-Object { $_.Launch }) -join ','))"
    Write-Output "races 2 and 3: each config differs from the previous race's on both cabinets (a new select)"
    Write-Output "cab1's report agrees with its stdout (agreed-match, RL-12, and end reason lines); end reasons $(($report.EndReasons | ForEach-Object { $_.Name }) -join ', ')"
    Write-Output "races 1..3: LR-8 triples of race ticks 0..2 equal across cab1 and cab2, one pin readback per race on both cabinets"
    Write-Output "race tick cap $raceTickCap on both cabinets (cab1's report and both stdouts)"
    Write-Output "arcade link launch check: PASS"
    Write-Output ("arcade link launch check: total time {0} s" -f [math]::Round(((Get-Date) - $checkStartedAt).TotalSeconds, 1))
    exit 0
}
finally {
    # On every exit and on an interrupt: no ctr_native this check started
    # outlives it.
    Stop-StartedRuns $runs
}
