[CmdletBinding()]
param(
    # Internal ctr_native build that accepts --arcade-roster-proof.
    [Parameter(Mandatory = $true)]
    [string]$Executable,

    # Absolute directory for the ten reports and their stdout/stderr logs.
    # Each run uses it as its working directory; the game itself still writes
    # the gitignored `Crash Team Racing.log` in the repository root.
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    # User-supplied disc image.  The check skips (exit 77) when it is absent.
    # Defaults to <repo>\assets\ctr-u.bin, resolved below: Windows PowerShell
    # 5.1 leaves $PSScriptRoot empty inside the param defaults of a
    # [CmdletBinding()] script run with -File.
    [string]$AssetsFile,

    # Race ticks each run (A-J) logs (--arcade-roster-proof-ticks).
    [int]$Ticks = 900,

    # Seconds all ten runs together may take (parallel), or each run
    # (-Sequential).
    [int]$TimeoutSeconds = 600,

    # Run the ten proofs one after another instead of all at once.
    [switch]$Sequential
)

# Live roster determinism check (docs/ROSTER_MILESTONE.md section 3.4, R-6,
# R-6b, R-6c, R-6d, and OC-3).  Runs ten live roster proofs, five of the
# two-cabinet profile (A-E) and five of the single-cabinet profile (F-J), and
# compares their reports:
#   A  two-cab, seed 0x5EED, dwell 0     (launches from the title)
#   B  two-cab, seed 0x5EED, dwell 0     (A again: byte-identical report)
#   C  two-cab, seed 0x5EED, dwell 5400  (launches from inside the attract
#                                         demo race)
#   D  two-cab, seed 0x5EEE, dwell 0     (another seed: a different config
#                                         digest, bank digest, and race tick 0
#                                         RNG digest)
#   E  two-cab, seed 0x5EED, dwell 37    (launches from the title 37 ticks
#                                         late, so its boot-relative timer is
#                                         an odd number of ticks off A's at
#                                         launch; the check verifies the
#                                         offset is odd)
#   F  one-cab, seed 0x5EED, dwell 0     (the retail 1P arcade race, one human
#                                         and seven bots, launched from the
#                                         title)
#   G  one-cab, seed 0x5EED, dwell 0     (F again: byte-identical report)
#   H  one-cab, seed 0x5EEE, dwell 0     (another seed: a different config
#                                         digest, bank digest, and race tick 0
#                                         RNG digest than F)
#   I  one-cab, seed 0x5EED, dwell 5400  (launches from inside the attract
#                                         demo race: the one-cab C, measured
#                                         against F)
#   J  one-cab, seed 0x5EED, dwell 37    (launches from the title 37 ticks
#                                         late, so its boot-relative timer is
#                                         an odd number of ticks off F's at
#                                         launch: the one-cab E; the check
#                                         verifies the offset is odd)
# A-E pass no --arcade-roster-proof-profile, so they run on the default
# profile (two-cab) with the command lines they had before OC-3, and their
# reports must say "profile TWO_CAB"; F-J pass --arcade-roster-proof-profile
# one-cab and must say "profile ONE_CAB".
#
# Tick counts.  Every run (A-J) logs -Ticks race ticks (900 in ctest), and
# all its per-run expectations (tick lines, "end ticks N", the tick
# comparisons) use that count.  The ONE_CAB cap (90 race ticks, which ended
# before the green light) was lifted once game/UI/UI_Rank.c's uninitialized
# pos.y read was fixed (commit a98dccbe8), so F-J now cover the green light,
# bot driving, and 1P race physics.
#
# Every report must be format v8 with result PASS, the profile line right
# after the result line, the expected launch window, both counter lines, a
# seeded line ending "match 1", eight slot lines, exactly the run's requested
# tick lines numbered from 0, and "end ticks N".  The slot lines must carry the
# profile's roles: TWO_CAB slot 0 CAB1_HUMAN, slot 1 CAB2_HUMAN, slots 2..5
# BOT (all present), slots 6..7 "role INACTIVE"; ONE_CAB slot 0 CAB1_HUMAN
# and slots 1..7 BOT, all present.  F differs from A in the config digest
# and the race plan digest (another profile).  The input digest (the raw pad
# state only) proves the hook installs the ONE_CAB pads: F matches A at race
# tick 0 (neutral pads, the same pad layout, RS-24), differs from A at every
# later tick (A's player 1 holds CROSS, F's is neutral), and
# matches H at every tick (the pads do not depend on the seed).
#
# C and E must match A in the config, race plan, bot setup plan, and bank
# digests, the seeded line, the slot lines, and at every race tick the rng,
# input, drivers, and race-relative control (rcontrol) digests.  rcontrol is
# the V1 control digest with the boot-relative counters (frameTimer,
# frameCounter, timer) zeroed; the full control digest also carries those
# counters.  The race setup pins gGT->timer and gGT->frameTimer_Confetti at
# race init (RS-17), so C and E must start the race with A's timer and
# frameTimerConfetti; the two counters it leaves (frameCounter and
# frameTimer, presentation and platform only, RS-17's audit) still differ,
# so the full control digest is informational and the check only reports it
# (RS-12).  The report header logs the four counters (those three and
# frameTimerConfetti) at the launch tick and at race tick 0, and the check
# prints the C-A and E-A offsets of both and their values mod 8.
#
# I and J are measured against F exactly as C and E are against A: J's timer
# offset from F at launch must be odd, I and J must start the race with F's
# timer and frameTimerConfetti (RS-17), and they must match F in the same
# setup evidence and per-tick digests, the full control digest again
# informational (RS-12).  The check prints the I-F and J-F offsets.
#
# Host timing.  The proof runs with the fixed VBlank pacing that main.c turns
# on for it (Platform_SetFixedVBlankPacing): a slow host frame emits no late
# VBlanks, so every game tick advances exactly the retail 2 VBlanks.  Under
# the default pacing, a host hitch would emit late VBlanks
# (Native_CatchUpDueVBlanks), raise gGT->elapsedTimeMS to 48 or 64 for that
# tick, and so change the race itself (elapsed and event time, the traffic
# lights, physics), not just the boot-relative VBlank count.  With the pacing
# fixed, the runs no longer depend on each other's timing and run in parallel
# by default; -Sequential runs them one after another.
#
# Every ctr_native the check started is stopped when the check exits or is
# interrupted.
#
# Exit codes: 0 pass, 1 fail, 77 skipped (ctest SKIP_RETURN_CODE).
$skipExitCode = 77
$noDisplayMarker = 'No displays available'
$notInternalMarker = '--arcade-roster-proof is available in internal builds only.'
$tickPattern = '^tick ([0-9]+) control ([0-9a-f]{16}) rcontrol ([0-9a-f]{16}) rng ([0-9a-f]{16}) input ([0-9a-f]{16}) drivers ([0-9a-f]{64})$'
$countersPattern = '^timer (-?[0-9]+) frameCounter (-?[0-9]+) frameTimer (-?[0-9]+) frameTimerConfetti (-?[0-9]+)$'
# The slot roles of each profile, slots 0..7 (NativeMatchConfigV1_InitArcadeTwoCab
# and _InitArcadeOneCab).
$slotRoles = @{
    'TWO_CAB' = @('CAB1_HUMAN', 'CAB2_HUMAN', 'BOT', 'BOT', 'BOT', 'BOT', 'INACTIVE', 'INACTIVE')
    'ONE_CAB' = @('CAB1_HUMAN', 'BOT', 'BOT', 'BOT', 'BOT', 'BOT', 'BOT', 'BOT')
}
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
    $arguments = @('--arcade-roster-proof', $Run.ReportPath, '--arcade-roster-proof-seed', $Run.Seed,
        '--arcade-roster-proof-dwell', "$($Run.Dwell)", '--arcade-roster-proof-ticks', "$($Run.Ticks)")
    # A-E use the default profile (two-cab); only the one-cab runs name it.
    if (-not [string]::IsNullOrEmpty($Run.ProfileOption)) {
        $arguments += @('--arcade-roster-proof-profile', $Run.ProfileOption)
    }
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
        Counters = $null
        LaunchCounters = $null
        Ticks = @()
        End = $null
        Problems = @()
    }
    foreach ($line in $lines) {
        if ($line -match $tickPattern) {
            $report.Ticks += [pscustomobject]@{
                Tick = [int]$Matches[1]; Control = $Matches[2]; RaceControl = $Matches[3]; Rng = $Matches[4]; Input = $Matches[5]
                Drivers = $Matches[6]; Line = $line
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
        elseif ($line -match '^(result|launch window|launch counters|race tick 0 counters|config digest|race plan digest|bot setup plan digest|bank digest) (.*)$') {
            $report.Header[$Matches[1]] = $Matches[2]
        }
    }
    if (($lines.Count -lt 2) -or ($lines[0] -ne 'arcade roster proof v8') -or ($lines[1] -ne 'drivers digest excludes physics')) {
        $report.Problems += 'the report does not start with the v8 header and "drivers digest excludes physics"'
    }
    if ($report.Header['result'] -ne 'PASS (0)') {
        $report.Problems += "result is '$($report.Header['result'])', not 'PASS (0)'"
    }
    # The profile line comes right after the result line.
    if (($lines.Count -lt 4) -or ($lines[2] -notmatch '^result ') -or ($lines[3] -ne "profile $($Run.Profile)")) {
        $found = ''
        if ($lines.Count -ge 4) {
            $found = $lines[3]
        }
        $report.Problems += "line 4 is '$found', expected 'profile $($Run.Profile)' right after the result line"
    }
    if (($null -ne $report.Header['race tick 0 counters']) -and ($report.Header['race tick 0 counters'] -match $countersPattern)) {
        $report.Counters = [pscustomobject]@{ Timer = [long]$Matches[1]; FrameCounter = [long]$Matches[2]; FrameTimer = [long]$Matches[3]
            FrameTimerConfetti = [long]$Matches[4] }
    }
    else {
        $report.Problems += "the race tick 0 counters line is missing or malformed: '$($report.Header['race tick 0 counters'])'"
    }
    if (($null -ne $report.Header['launch counters']) -and ($report.Header['launch counters'] -match $countersPattern)) {
        $report.LaunchCounters = [pscustomobject]@{ Timer = [long]$Matches[1]; FrameCounter = [long]$Matches[2]; FrameTimer = [long]$Matches[3]
            FrameTimerConfetti = [long]$Matches[4] }
    }
    else {
        $report.Problems += "the launch counters line is missing or malformed: '$($report.Header['launch counters'])'"
    }
    if ($report.Ticks.Count -ne $Run.Ticks) {
        $report.Problems += "$($report.Ticks.Count) tick lines, expected $($Run.Ticks)"
    }
    for ($i = 0; $i -lt $report.Ticks.Count; $i++) {
        if ($report.Ticks[$i].Tick -ne $i) {
            $report.Problems += "tick line $i is numbered $($report.Ticks[$i].Tick)"
            break
        }
    }
    if (($lines.Count -eq 0) -or ($lines[$lines.Count - 1] -ne "end ticks $($Run.Ticks)")) {
        $report.Problems += "the report does not end with 'end ticks $($Run.Ticks)'"
    }
    if (($null -eq $report.Seeded) -or (-not $report.Seeded.EndsWith(' match 1'))) {
        $report.Problems += "the seeded line is missing or does not match: '$($report.Seeded)'"
    }
    if ($report.Slots.Count -ne 8) {
        $report.Problems += "$($report.Slots.Count) slot lines, expected 8"
    }
    else {
        # The profile's slot roles: a present slot is "slot N role R character
        # ...", an absent one "slot N role R" (FormatReport).
        $roles = $slotRoles[$Run.Profile]
        for ($i = 0; $i -lt 8; $i++) {
            $role = $roles[$i]
            if ($role -eq 'INACTIVE') {
                $ok = $report.Slots[$i] -eq "slot $i role INACTIVE"
                $expected = "slot $i role INACTIVE"
            }
            else {
                $ok = $report.Slots[$i] -match "^slot $i role $role character [0-9]+ difficulty 0x[0-9A-F]{2} spawn [0-9]+ nav [0-9]+ accel [0-9]+$"
                $expected = "slot $i role $role character ... (present)"
            }
            if (-not $ok) {
                $report.Problems += "slot line $i is '$($report.Slots[$i])', expected '$expected' for $($Run.Profile)"
            }
        }
    }
    return $report
}

# 1 when the two files are byte-identical; otherwise the first differing
# line of the two parsed reports.
function Compare-ReportBytes($PathX, $PathY, $ReportX, $ReportY, [string]$NameX, [string]$NameY) {
    $bytesX = [System.IO.File]::ReadAllBytes($PathX)
    $bytesY = [System.IO.File]::ReadAllBytes($PathY)
    $identical = $bytesX.Length -eq $bytesY.Length
    for ($i = 0; $identical -and ($i -lt $bytesX.Length); $i++) {
        if ($bytesX[$i] -ne $bytesY[$i]) {
            $identical = $false
        }
    }
    $firstLine = $null
    if (-not $identical) {
        for ($i = 0; $i -lt [Math]::Min($ReportX.Lines.Count, $ReportY.Lines.Count); $i++) {
            if ($ReportX.Lines[$i] -ne $ReportY.Lines[$i]) {
                $firstLine = "line $($i + 1): $NameX '$($ReportX.Lines[$i])' $NameY '$($ReportY.Lines[$i])'"
                break
            }
        }
    }
    return [pscustomobject]@{ Identical = $identical; Length = $bytesX.Length; FirstLine = $firstLine }
}

# x mod 8 in 0..7, also for a negative x.
function Get-Mod8([long]$Value) {
    return (($Value % 8) + 8) % 8
}

# The boot-relative counter offsets of $Other from $A (their Counters or,
# with -Launch, their LaunchCounters), printed with mod 8.
function Get-CounterOffsets($A, $Other, [switch]$Launch) {
    $countersA = $A.Counters
    $countersOther = $Other.Counters
    if ($Launch) {
        $countersA = $A.LaunchCounters
        $countersOther = $Other.LaunchCounters
    }
    $timer = $countersOther.Timer - $countersA.Timer
    $frameCounter = $countersOther.FrameCounter - $countersA.FrameCounter
    $frameTimer = $countersOther.FrameTimer - $countersA.FrameTimer
    $frameTimerConfetti = $countersOther.FrameTimerConfetti - $countersA.FrameTimerConfetti
    return [pscustomobject]@{
        Timer = $timer
        FrameCounter = $frameCounter
        FrameTimer = $frameTimer
        FrameTimerConfetti = $frameTimerConfetti
        Text = (("timer {0:+#;-#;0} (mod 8 = {1}), frameCounter {2:+#;-#;0} (mod 8 = {3}), frameTimer {4:+#;-#;0} (mod 8 = {5}), " +
            "frameTimerConfetti {6:+#;-#;0} (mod 8 = {7})") -f $timer, (Get-Mod8 $timer), $frameCounter, (Get-Mod8 $frameCounter),
            $frameTimer, (Get-Mod8 $frameTimer), $frameTimerConfetti, (Get-Mod8 $frameTimerConfetti))
    }
}

# Compares $Other with the reference report $Base (named $BaseName) as C
# and E must match A, and I and J must match F: the setup evidence and, at
# every race tick, the rng, input, drivers, and rcontrol digests.  The full
# control digest is informational.  Returns the failures and the messages
# to print.
function Compare-WithBase($Base, [string]$BaseName, $Other, [string]$Name) {
    $found = @()
    $messages = @()
    foreach ($key in @('config digest', 'race plan digest', 'bot setup plan digest', 'bank digest')) {
        if ($Other.Header[$key] -ne $Base.Header[$key]) {
            $found += "$Name differs from $BaseName in the $key ($BaseName $($Base.Header[$key]), $Name $($Other.Header[$key]))"
        }
    }
    if ($Other.Seeded -ne $Base.Seeded) {
        $found += "$Name differs from $BaseName in the seeded line ($BaseName '$($Base.Seeded)', $Name '$($Other.Seeded)')"
    }
    for ($i = 0; $i -lt 8; $i++) {
        if ($Other.Slots[$i] -ne $Base.Slots[$i]) {
            $found += "$Name differs from $BaseName in slot line $i ($BaseName '$($Base.Slots[$i])', $Name '$($Other.Slots[$i])')"
        }
    }
    $controlDifferences = 0
    $firstControl = $null
    $firstSimulation = $null
    for ($i = 0; $i -lt $Ticks; $i++) {
        $tickBase = $Base.Ticks[$i]
        $tickOther = $Other.Ticks[$i]
        if ($tickBase.Control -ne $tickOther.Control) {
            $controlDifferences++
            if ($null -eq $firstControl) {
                $firstControl = $i
            }
        }
        if ($null -eq $firstSimulation) {
            foreach ($domain in @('Rng', 'Input', 'Drivers', 'RaceControl')) {
                if ($tickBase.$domain -ne $tickOther.$domain) {
                    $label = $domain.ToLowerInvariant()
                    if ($domain -eq 'RaceControl') {
                        $label = 'rcontrol'
                    }
                    $firstSimulation = "tick $i $label ($BaseName $($tickBase.$domain), $Name $($tickOther.$domain))"
                    break
                }
            }
        }
    }
    if ($null -ne $firstSimulation) {
        $found += "$Name differs from $BaseName at $firstSimulation"
    }
    elseif ($found.Count -eq 0) {
        $messages += "$Name = ${BaseName}: config, race plan, bot setup, and bank digests, the seeded line, 8 slot lines, and the rng, input, drivers, and rcontrol digests of all $Ticks ticks"
    }
    if ($controlDifferences -eq 0) {
        $messages += "$Name vs $BaseName control digests (informational, RS-12): identical at all $Ticks ticks"
    }
    else {
        $messages += "$Name vs $BaseName control digests (informational, RS-12): $controlDifferences of $Ticks ticks differ (first at tick $firstControl); the unpinned boot-relative counters frameCounter and frameTimer (RS-17)"
    }
    return [pscustomobject]@{ Failures = $found; Messages = $messages }
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
    if (($Ticks -lt 1) -or ($Ticks -gt 3600)) {
        Exit-Failed "invalid tick count $Ticks (1..3600)"
    }
    $resolvedExecutable = (Resolve-Path -LiteralPath $Executable -ErrorAction Stop).Path
    $resolvedOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
    [System.IO.Directory]::CreateDirectory($resolvedOutput) | Out-Null

    # ProfileOption is the --arcade-roster-proof-profile value, or '' for the
    # default (two-cab); Profile is the report's expected profile line; Ticks
    # is the run's --arcade-roster-proof-ticks (-Ticks for every run).
    $specs = @(
        @{ Name = 'A'; Profile = 'TWO_CAB'; ProfileOption = ''; Seed = '0x5EED'; Dwell = 0; Window = 'title'; Ticks = $Ticks },
        @{ Name = 'B'; Profile = 'TWO_CAB'; ProfileOption = ''; Seed = '0x5EED'; Dwell = 0; Window = 'title'; Ticks = $Ticks },
        @{ Name = 'C'; Profile = 'TWO_CAB'; ProfileOption = ''; Seed = '0x5EED'; Dwell = 5400; Window = 'demo race'; Ticks = $Ticks },
        @{ Name = 'D'; Profile = 'TWO_CAB'; ProfileOption = ''; Seed = '0x5EEE'; Dwell = 0; Window = 'title'; Ticks = $Ticks },
        @{ Name = 'E'; Profile = 'TWO_CAB'; ProfileOption = ''; Seed = '0x5EED'; Dwell = 37; Window = 'title'; Ticks = $Ticks },
        @{ Name = 'F'; Profile = 'ONE_CAB'; ProfileOption = 'one-cab'; Seed = '0x5EED'; Dwell = 0; Window = 'title'; Ticks = $Ticks },
        @{ Name = 'G'; Profile = 'ONE_CAB'; ProfileOption = 'one-cab'; Seed = '0x5EED'; Dwell = 0; Window = 'title'; Ticks = $Ticks },
        @{ Name = 'H'; Profile = 'ONE_CAB'; ProfileOption = 'one-cab'; Seed = '0x5EEE'; Dwell = 0; Window = 'title'; Ticks = $Ticks },
        @{ Name = 'I'; Profile = 'ONE_CAB'; ProfileOption = 'one-cab'; Seed = '0x5EED'; Dwell = 5400; Window = 'demo race'; Ticks = $Ticks },
        @{ Name = 'J'; Profile = 'ONE_CAB'; ProfileOption = 'one-cab'; Seed = '0x5EED'; Dwell = 37; Window = 'title'; Ticks = $Ticks })
    foreach ($spec in $specs) {
        $run = [pscustomobject]@{
            Name = $spec.Name
            Profile = $spec.Profile
            ProfileOption = $spec.ProfileOption
            Seed = $spec.Seed
            Dwell = $spec.Dwell
            Window = $spec.Window
            Ticks = $spec.Ticks
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

    $mode = 'parallel'
    if ($Sequential) {
        $mode = 'sequential'
    }
    Write-Output "arcade roster determinism check: $($runs.Count) runs ($mode, fixed VBlank pacing), $Ticks race ticks each, five two-cab (A-E) and five one-cab (F-J)"
    Write-Output "executable: $resolvedExecutable"
    Write-Output "output:     $resolvedOutput"

    $checkStartedAt = Get-Date
    if ($Sequential) {
        foreach ($run in $runs) {
            $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
            Start-Run $run
            Wait-Runs @($run) $runs
        }
    }
    else {
        $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
        foreach ($run in $runs) {
            Start-Run $run
        }
        Wait-Runs $runs $runs
    }

    $failures = @()
    foreach ($run in $runs) {
        $exitCode = $run.Process.ExitCode
        $seconds = [math]::Round(($run.Process.ExitTime - $run.StartedAt).TotalSeconds, 1)
        Write-Output ("run {0} {1} seed {2} dwell {3,-4} exit {4} in {5} s" -f $run.Name, $run.Profile, $run.Seed, $run.Dwell, $exitCode, $seconds)
        if ($exitCode -ne 0) {
            $failures += "run $($run.Name) exited $exitCode; see $($run.StdoutPath) and $($run.StderrPath)"
        }
        elseif (-not (Test-Path -LiteralPath $run.ReportPath -PathType Leaf)) {
            $failures += "run $($run.Name) exited 0 but wrote no report $($run.ReportPath)"
        }
    }
    Write-Output ("all runs done in {0} s" -f [math]::Round(((Get-Date) - $checkStartedAt).TotalSeconds, 1))
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
    $e = $reports['E']
    $f = $reports['F']
    $g = $reports['G']
    $h = $reports['H']
    # Not $i and $j: those are the loop counters below.
    $reportI = $reports['I']
    $reportJ = $reports['J']
    $runByName = @{}
    foreach ($run in $runs) {
        $runByName[$run.Name] = $run
    }

    # A and B, and F and G: byte-identical.
    foreach ($pair in @(@('A', 'B'), @('F', 'G'))) {
        $comparison = Compare-ReportBytes $runByName[$pair[0]].ReportPath $runByName[$pair[1]].ReportPath `
            $reports[$pair[0]] $reports[$pair[1]] $pair[0] $pair[1]
        if ($comparison.Identical) {
            Write-Output "$($pair[0]) = $($pair[1]): byte-identical ($($comparison.Length) bytes)"
        }
        else {
            $failures += "$($pair[0]) and $($pair[1]) reports differ (first difference $($comparison.FirstLine))"
        }
    }

    # The boot-relative counters at the launch tick (the boot history each
    # run brings) and at race tick 0, and the offsets from A.  E must bring
    # an odd timer offset, and the race setup's pins (RS-17) must remove it:
    # C and E start the race with A's timer and frameTimerConfetti.
    Write-Output "A launch counters: $($a.Header['launch counters'])"
    Write-Output "A race tick 0 counters: $($a.Header['race tick 0 counters'])"
    $launchC = Get-CounterOffsets $a $c -Launch
    $launchE = Get-CounterOffsets $a $e -Launch
    $offsetsC = Get-CounterOffsets $a $c
    $offsetsE = Get-CounterOffsets $a $e
    Write-Output "C - A offsets at launch: $($launchC.Text)"
    Write-Output "E - A offsets at launch: $($launchE.Text)"
    Write-Output "C - A offsets at race tick 0: $($offsetsC.Text)"
    Write-Output "E - A offsets at race tick 0: $($offsetsE.Text)"
    if (($launchE.Timer % 2) -eq 0) {
        $failures += "E's timer offset from A at launch is $($launchE.Timer), not odd: run E no longer covers an odd boot-relative offset (choose another dwell)"
    }
    foreach ($pair in @(@('C', $offsetsC), @('E', $offsetsE))) {
        if ($pair[1].Timer -ne 0) {
            $failures += "$($pair[0])'s timer at race tick 0 is $($pair[1].Timer) off A's: the race setup's timer pin (RS-17) did not hold"
        }
        if ($pair[1].FrameTimerConfetti -ne 0) {
            $failures += "$($pair[0])'s frameTimerConfetti at race tick 0 is $($pair[1].FrameTimerConfetti) off A's: the race setup's frameTimer_Confetti pin (RS-17) did not hold"
        }
    }

    # C and E against A.
    foreach ($pair in @(@('C', $c), @('E', $e))) {
        $comparison = Compare-WithBase $a 'A' $pair[1] $pair[0]
        foreach ($message in $comparison.Messages) {
            Write-Output $message
        }
        $failures += @($comparison.Failures)
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

    # H against F: another seed changes the ONE_CAB config, bank, and race tick 0's RNG.
    $hDiffers = $true
    foreach ($key in @('config digest', 'bank digest')) {
        if ($h.Header[$key] -eq $f.Header[$key]) {
            $failures += "H does not differ from F in the $key ($($f.Header[$key]))"
            $hDiffers = $false
        }
    }
    if ($h.Ticks[0].Rng -eq $f.Ticks[0].Rng) {
        $failures += "H does not differ from F in the tick 0 rng digest ($($f.Ticks[0].Rng))"
        $hDiffers = $false
    }
    if ($hDiffers) {
        Write-Output "H != F: config digest, bank digest, and tick 0 rng digest (F $($f.Ticks[0].Rng), H $($h.Ticks[0].Rng))"
    }

    # F against A: another profile changes the config and the race plan.
    $fDiffers = $true
    foreach ($key in @('config digest', 'race plan digest')) {
        if ($f.Header[$key] -eq $a.Header[$key]) {
            $failures += "F does not differ from A in the $key ($($a.Header[$key]))"
            $fDiffers = $false
        }
    }
    if ($fDiffers) {
        Write-Output "F != A: config digest and race plan digest (ONE_CAB vs TWO_CAB)"
    }

    # The ONE_CAB pads reach the game.  The input digest is the V1 INPUT
    # domain digest: the raw pad state only (NativeCanonicalInputV1: each
    # pad's status, id, buttons, analog, and connected).  Race tick 0 runs on
    # neutral pads in both profiles (the same pad layout), so F = A there;
    # from race tick 1 A's player 1 holds CROSS and F's player 1 is neutral,
    # so F != A at every later tick; and the pads are a function of (profile,
    # tick) only, so F = H at every tick.
    $inputFailures = @()
    if ($f.Ticks[0].Input -ne $a.Ticks[0].Input) {
        $inputFailures += "F differs from A in the tick 0 input digest (A $($a.Ticks[0].Input), F $($f.Ticks[0].Input)): the neutral pads of the two profiles are not the same pad layout"
    }
    for ($i = 1; $i -lt $Ticks; $i++) {
        if ($f.Ticks[$i].Input -eq $a.Ticks[$i].Input) {
            $inputFailures += "F does not differ from A in the tick $i input digest ($($a.Ticks[$i].Input)): the ONE_CAB pads did not reach the game"
            break
        }
    }
    for ($i = 0; $i -lt $Ticks; $i++) {
        if ($f.Ticks[$i].Input -ne $h.Ticks[$i].Input) {
            $inputFailures += "F differs from H in the tick $i input digest (F $($f.Ticks[$i].Input), H $($h.Ticks[$i].Input)): the pads depend on the seed"
            break
        }
    }
    if ($inputFailures.Count -eq 0) {
        Write-Output "F input digests: = A at tick 0 (neutral, same pad layout), != A at ticks 1..$($Ticks - 1) (ONE_CAB pads), = H at all $Ticks ticks (seed-independent)"
    }
    $failures += $inputFailures

    # I and J against F, as C and E against A: J must bring an odd timer
    # offset from F at launch, the RS-17 pins must remove it (I and J start
    # the race with F's timer and frameTimerConfetti), and I and J must match
    # F in the setup evidence and the per-tick digests.
    Write-Output "F launch counters: $($f.Header['launch counters'])"
    Write-Output "F race tick 0 counters: $($f.Header['race tick 0 counters'])"
    $launchI = Get-CounterOffsets $f $reportI -Launch
    $launchJ = Get-CounterOffsets $f $reportJ -Launch
    $offsetsI = Get-CounterOffsets $f $reportI
    $offsetsJ = Get-CounterOffsets $f $reportJ
    Write-Output "I - F offsets at launch: $($launchI.Text)"
    Write-Output "J - F offsets at launch: $($launchJ.Text)"
    Write-Output "I - F offsets at race tick 0: $($offsetsI.Text)"
    Write-Output "J - F offsets at race tick 0: $($offsetsJ.Text)"
    if (($launchJ.Timer % 2) -eq 0) {
        $failures += "J's timer offset from F at launch is $($launchJ.Timer), not odd: run J no longer covers an odd boot-relative offset (choose another dwell)"
    }
    foreach ($pair in @(@('I', $offsetsI), @('J', $offsetsJ))) {
        if ($pair[1].Timer -ne 0) {
            $failures += "$($pair[0])'s timer at race tick 0 is $($pair[1].Timer) off F's: the race setup's timer pin (RS-17) did not hold"
        }
        if ($pair[1].FrameTimerConfetti -ne 0) {
            $failures += "$($pair[0])'s frameTimerConfetti at race tick 0 is $($pair[1].FrameTimerConfetti) off F's: the race setup's frameTimer_Confetti pin (RS-17) did not hold"
        }
    }
    foreach ($pair in @(@('I', $reportI), @('J', $reportJ))) {
        $comparison = Compare-WithBase $f 'F' $pair[1] $pair[0]
        foreach ($message in $comparison.Messages) {
            Write-Output $message
        }
        $failures += @($comparison.Failures)
    }

    Write-Output "report A first tick: $($a.Ticks[0].Line)"
    Write-Output "report A last tick:  $($a.Ticks[$Ticks - 1].Line)"
    Write-Output "report F first tick: $($f.Ticks[0].Line)"
    Write-Output "report F last tick:  $($f.Ticks[$Ticks - 1].Line)"
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
}
finally {
    # On every exit and on an interrupt: no ctr_native this check started
    # outlives it.
    Stop-StartedRuns $runs
}
