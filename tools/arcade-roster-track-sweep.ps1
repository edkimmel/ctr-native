[CmdletBinding()]
param(
    # Internal ctr_native build that accepts --arcade-roster-proof.
    [Parameter(Mandatory = $true)]
    [string]$Executable,

    # Absolute directory for the sweep.  Each run gets its own subdirectory
    # (track<NN>-<profile>, and track<NN>-<profile>-b for the second run of a
    # -Pairs pair) holding its report and stdout/stderr logs, and
    # uses it as its working directory; the game itself still writes the
    # gitignored `Crash Team Racing.log` in the repository root.
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    # User-supplied disc image.  The sweep skips (exit 77) when it is absent.
    # Defaults to <repo>\assets\ctr-u.bin, resolved below: Windows PowerShell
    # 5.1 leaves $PSScriptRoot empty inside the param defaults of a
    # [CmdletBinding()] script run with -File.
    [string]$AssetsFile,

    # Race ticks each run logs (--arcade-roster-proof-ticks): 1..6000 for
    # two-cab (the autopilot allows 6000), 1..3600 for one-cab.
    [int]$Ticks = 1200,

    # Which profiles run on every track: two-cab (with the autopilot), one-cab,
    # or both.
    [ValidateSet('two-cab', 'one-cab', 'both')]
    [string]$Profile = 'both',

    # Which profiles run twice on every track as a same-seed pair (see
    # below): none, two-cab, or both (two-cab and one-cab).  Every profile it
    # names must be one -Profile runs.
    [ValidateSet('none', 'two-cab', 'both')]
    [string]$Pairs = 'none',

    # The tracks (levelIDs) to run, each one of the 16 arcade match-select
    # tracks; default all 16, in the retail menu order.  Accepts a list
    # ("-Tracks 0,12" also works through powershell -File, which passes it as
    # one string).
    [string[]]$Tracks,

    # Most ctr_native processes running at once.
    [int]$Parallel = 8,

    # Seconds each run may take from its start; a run over it is stopped and
    # fails.
    [int]$TimeoutSeconds = 600,

    # Print the planned runs (track, profile, command-line flags) and exit 0
    # without launching anything (tests/arcade_roster_track_sweep_plan_test.cmake).
    [switch]$ListRuns
)

# Arcade roster track sweep: one live roster proof (--arcade-roster-proof)
# per (track, profile), so the V4 drivers extraction and the rest of the
# proof's per-tick evidence are exercised on every arcade track, not just the
# fixture's track 3 of tools/arcade-roster-proof-check.ps1.
#
# Each run uses the checker's command-line conventions for its run A (two-cab)
# and F (one-cab): seed 0x5EED, dwell 0 (launch from the title), -Ticks race
# ticks, and --arcade-roster-proof-track <T>.  The two-cab run adds
# --arcade-roster-proof-autopilot (players 0 and 1 steer toward the restart
# points ahead, LR-S2 (b)), which is what drives the humans into the tracks'
# hazards (the Dingo Canyon armadillos, the Polar Pass seals).  The one-cab
# run passes --arcade-roster-proof-profile one-cab and runs the scripted pads
# (the autopilot is two-cab only).
#
# Each run's report must be format v12 with "result PASS (0)", the profile
# line (TWO_CAB or ONE_CAB) right after the result line, "ticks N", then
# "track T", exactly N tick lines numbered from 0, and "end ticks N"; the
# process must exit 0.  A failing run prints its result line and the tail of
# its stdout (the proof logs its failing check there).  The sweep prints one
# summary row per run and a total, and exits nonzero if any run failed.
#
# Unpaired runs do not compare with each other: the proof's fixed VBlank
# pacing makes each run independent of the others' timing, so they run
# -Parallel at a time.  Every ctr_native the sweep started is stopped when it
# exits or is interrupted.
#
# Same-seed pairs (-Pairs): each (track, profile) whose profile -Pairs names
# runs twice, run a in track<NN>-<profile> and run b in
# track<NN>-<profile>-b, with byte-identical game arguments except the report
# path; b is queued right after a.  Once both runs of a pair passed the checks
# above, the pair compares their WHOLE reports byte-for-byte (the report holds
# no run-specific text: not its path, not a wall-clock time; the checker's
# A = B and F = G compare whole reports the same way).  Any difference fails
# the pair; the sweep then prints the first differing tick line (ordinal
# compare of the tick lines in order), the digest fields that differ there
# (control, rcontrol, rng, input, drivers, v4, v4control, ...), and both
# lines, or the first differing non-tick line when every tick line matches.
# A two-cab pair also requires the stdout autopilot summary lines
# ("autopilot: seed ... END_OF_RACE tick E player 0 finish tick P0 player 1
# finish tick P1 (-1: never; N race ticks)", game/MAIN/MainArcadeRosterProof.c)
# of a and b to be present and equal.  The summary shows each two-cab run's
# END_OF_RACE and finish ticks (paired or not; '-' when the line is absent,
# which fails only a pair) and each pair's identity: a==b, DIFF@<tick>,
# DIFF@line<N> (a non-tick report line), DIFF@finish (the autopilot lines),
# n/a (a run of the pair failed, not compared), or - (unpaired).
#
# Exit codes: 0 pass, 1 fail, 77 skipped (ctest SKIP_RETURN_CODE).
$skipExitCode = 77
$noDisplayMarker = 'No displays available'
$notInternalMarker = '--arcade-roster-proof is available in internal builds only.'
$tickPattern = '^tick ([0-9]+) control [0-9a-f]{16} rcontrol [0-9a-f]{16} rng [0-9a-f]{16} input [0-9a-f]{16} drivers [0-9a-f]{64} v4 [0-9a-f]{16} v4control [0-9a-f]{16} v4rng [0-9a-f]{16} v4input [0-9a-f]{16} v4drivers [0-9a-f]{16} v4world [0-9a-f]{16} v4topology [0-9a-f]{16}$'
# The two-cab autopilot's stdout summary, logged once after the last race tick.
$autopilotPattern = 'autopilot: seed 0x[0-9A-F]{16} END_OF_RACE tick (-?[0-9]+) player 0 finish tick (-?[0-9]+) player 1 finish tick (-?[0-9]+) \(-1: never; [0-9]+ race ticks\)'
$seed = '0x5EED'
$dwell = 0
# The 16 arcade match-select tracks, in the retail menu order: the levelIDs of
# the arcadeTracks rows (game/230/D230.c:561-599) with unlock 0xFFFF, as
# NativeMatchSelect_TrackAt returns them (platform/native_match_select_rules.c
# k_matchSelectTracks; the plan test checks the two agree).  OXIDE_STATION
# (13) and TURBO_TRACK (17) are not offered.
$trackTable = @(
    @{ Id = 3; Name = 'Crash Cove' },
    @{ Id = 6; Name = 'Roo''s Tubes' },
    @{ Id = 4; Name = 'Tiger Temple' },
    @{ Id = 14; Name = 'Coco Park' },
    @{ Id = 9; Name = 'Mystery Caves' },
    @{ Id = 2; Name = 'Blizzard Bluff' },
    @{ Id = 8; Name = 'Sewer Speedway' },
    @{ Id = 0; Name = 'Dingo Canyon' },
    @{ Id = 5; Name = 'Papu''s Pyramid' },
    @{ Id = 1; Name = 'Dragon Mines' },
    @{ Id = 12; Name = 'Polar Pass' },
    @{ Id = 10; Name = 'Cortex Castle' },
    @{ Id = 15; Name = 'Tiny Arena' },
    @{ Id = 7; Name = 'Hot Air Skyway' },
    @{ Id = 11; Name = 'N. Gin Labs' },
    @{ Id = 16; Name = 'Slide Coliseum' })
# Per profile: the report's profile line, the extra flags, and the tick limit
# (--arcade-roster-proof-ticks: 3600, or 6000 with the autopilot).
$profileTable = @{
    'two-cab' = @{ Report = 'TWO_CAB'; Flags = @('--arcade-roster-proof-autopilot'); MaxTicks = 6000 }
    'one-cab' = @{ Report = 'ONE_CAB'; Flags = @('--arcade-roster-proof-profile', 'one-cab'); MaxTicks = 3600 }
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
        return $text
    }
    finally {
        $stream.Dispose()
    }
}

# Stops every run this sweep started that is still running.
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

function Start-SweepRun($Run) {
    [System.IO.Directory]::CreateDirectory($Run.Directory) | Out-Null
    # A stale report from an earlier sweep must never pass for this one.
    foreach ($stale in @($Run.ReportPath, $Run.StdoutPath, $Run.StderrPath)) {
        if (Test-Path -LiteralPath $stale) {
            Remove-Item -LiteralPath $stale -Force -ErrorAction Stop
        }
    }
    $argumentLine = ($Run.Arguments | ForEach-Object { ConvertTo-ProcessArgument $_ }) -join ' '
    $process = Start-Process -FilePath $resolvedExecutable -ArgumentList $argumentLine `
        -WorkingDirectory $Run.Directory -NoNewWindow -PassThru `
        -RedirectStandardOutput $Run.StdoutPath -RedirectStandardError $Run.StderrPath
    # Touching Handle keeps ExitCode readable after the process exits.
    $null = $process.Handle
    $Run.Process = $process
    $Run.StartedAt = Get-Date
}

# Checks a finished (or stopped) run's report: sets Result, Reached, and
# Problems.
function Test-SweepRun($Run) {
    $problems = @()
    if ($Run.TimedOut) {
        $problems += "timed out after $TimeoutSeconds s and was stopped"
    }
    elseif ($Run.Process.ExitCode -ne 0) {
        $problems += "exited $($Run.Process.ExitCode)"
    }
    if (-not (Test-Path -LiteralPath $Run.ReportPath -PathType Leaf)) {
        $problems += "wrote no report $($Run.ReportPath)"
        if ($Run.TimedOut) {
            $Run.Result = 'TIMEOUT'
        }
        else {
            $Run.Result = 'NO_REPORT'
        }
        $Run.Problems = $problems
        return
    }
    $lines = @([System.IO.File]::ReadAllLines($Run.ReportPath))
    $resultLine = ''
    if (($lines.Count -ge 3) -and ($lines[2] -match '^result ([A-Z_0-9]+) \(([0-9]+)\)$')) {
        $resultLine = $lines[2]
        $Run.Result = $Matches[1]
    }
    else {
        $Run.Result = 'MALFORMED'
    }
    $Run.ResultLine = $resultLine
    if (($lines.Count -lt 2) -or ($lines[0] -ne 'arcade roster proof v12') -or ($lines[1] -ne 'drivers digest excludes physics')) {
        $problems += 'the report does not start with the v12 header and "drivers digest excludes physics"'
    }
    if ($resultLine -ne 'result PASS (0)') {
        $problems += "result line is '$resultLine', not 'result PASS (0)'"
    }
    if (($lines.Count -lt 4) -or ($lines[3] -ne "profile $($Run.ReportProfile)")) {
        $found = ''
        if ($lines.Count -ge 4) {
            $found = $lines[3]
        }
        $problems += "line 4 is '$found', expected 'profile $($Run.ReportProfile)' right after the result line"
    }
    if (($lines.Count -lt 10) -or ($lines[8] -ne "ticks $Ticks") -or ($lines[9] -ne "track $($Run.Track)")) {
        $found = ''
        if ($lines.Count -ge 10) {
            $found = "'$($lines[8])', '$($lines[9])'"
        }
        $problems += "lines 9 and 10 are $found, expected 'ticks $Ticks' then 'track $($Run.Track)'"
    }
    $tickCount = 0
    $numbered = $true
    foreach ($line in $lines) {
        if ($line -match $tickPattern) {
            if ([int]$Matches[1] -ne $tickCount) {
                $numbered = $false
            }
            $tickCount++
        }
    }
    $Run.Reached = $tickCount
    if ($tickCount -ne $Ticks) {
        $problems += "$tickCount tick lines, expected $Ticks"
    }
    if (-not $numbered) {
        $problems += 'the tick lines are not numbered 0, 1, 2, ...'
    }
    if (($lines.Count -eq 0) -or ($lines[$lines.Count - 1] -ne "end ticks $Ticks")) {
        $problems += "the report does not end with 'end ticks $Ticks'"
    }
    $Run.Problems = $problems
}

# Reads a two-cab run's autopilot summary from its stdout: sets Autopilot (the
# summary text of every match, joined; '' when absent) and EndOfRace,
# FinishP0, and FinishP1 (from the last match; '-' when absent).
function Read-AutopilotSummary($Run) {
    $Run.Autopilot = ''
    $Run.EndOfRace = '-'
    $Run.FinishP0 = '-'
    $Run.FinishP1 = '-'
    if ($Run.Profile -ne 'two-cab') {
        return
    }
    $found = @([regex]::Matches((Read-SharedText $Run.StdoutPath), $autopilotPattern))
    if ($found.Count -eq 0) {
        return
    }
    $Run.Autopilot = ($found | ForEach-Object { $_.Value }) -join "`n"
    $last = $found[$found.Count - 1]
    $Run.EndOfRace = $last.Groups[1].Value
    $Run.FinishP0 = $last.Groups[2].Value
    $Run.FinishP1 = $last.Groups[3].Value
}

# The digest fields of two tick lines ("tick N name value name value ...")
# whose values differ, in line order.
function Get-TickFieldDifferences([string]$LineA, [string]$LineB) {
    $tokensA = $LineA.Split(' ')
    $tokensB = $LineB.Split(' ')
    $fields = @()
    for ($i = 2; ($i + 1) -lt [math]::Min($tokensA.Count, $tokensB.Count); $i += 2) {
        if (-not [string]::Equals($tokensA[$i + 1], $tokensB[$i + 1], [System.StringComparison]::Ordinal)) {
            $fields += $tokensA[$i]
        }
    }
    return $fields
}

# Compares the two runs of a pair, both finished and passing: sets both runs'
# Identity and returns the pair's problems (none when identical).
function Compare-SweepPair($RunA, $RunB) {
    $problems = @()
    $identity = 'a==b'
    $linesA = @([System.IO.File]::ReadAllLines($RunA.ReportPath))
    $linesB = @([System.IO.File]::ReadAllLines($RunB.ReportPath))
    $ticksA = @($linesA | Where-Object { $_ -cmatch $tickPattern })
    $ticksB = @($linesB | Where-Object { $_ -cmatch $tickPattern })
    $count = [math]::Min($ticksA.Count, $ticksB.Count)
    for ($i = 0; $i -lt $count; $i++) {
        if (-not [string]::Equals($ticksA[$i], $ticksB[$i], [System.StringComparison]::Ordinal)) {
            $tick = $i
            if ($ticksA[$i] -match '^tick ([0-9]+) ') {
                $tick = [int]$Matches[1]
            }
            $identity = "DIFF@$tick"
            $fields = @(Get-TickFieldDifferences $ticksA[$i] $ticksB[$i])
            $problems += "first differing tick $tick, fields: $($fields -join ', ')"
            $problems += "a: $($ticksA[$i])"
            $problems += "b: $($ticksB[$i])"
            break
        }
    }
    if (($identity -eq 'a==b') -and ($ticksA.Count -ne $ticksB.Count)) {
        $identity = "DIFF@$count"
        $problems += "a has $($ticksA.Count) tick lines, b $($ticksB.Count)"
    }
    if ($identity -eq 'a==b') {
        # Every tick line matches: the whole report, byte-for-byte.
        $bytesA = [System.IO.File]::ReadAllBytes($RunA.ReportPath)
        $bytesB = [System.IO.File]::ReadAllBytes($RunB.ReportPath)
        $same = $bytesA.Length -eq $bytesB.Length
        for ($i = 0; $same -and ($i -lt $bytesA.Length); $i++) {
            if ($bytesA[$i] -ne $bytesB[$i]) {
                $same = $false
            }
        }
        if (-not $same) {
            $lineCount = [math]::Max($linesA.Count, $linesB.Count)
            $line = $lineCount
            for ($i = 0; $i -lt $lineCount; $i++) {
                $textA = ''
                $textB = ''
                if ($i -lt $linesA.Count) {
                    $textA = $linesA[$i]
                }
                if ($i -lt $linesB.Count) {
                    $textB = $linesB[$i]
                }
                if (($i -ge $linesA.Count) -or ($i -ge $linesB.Count) -or
                    (-not [string]::Equals($textA, $textB, [System.StringComparison]::Ordinal))) {
                    $line = $i
                    break
                }
            }
            $identity = "DIFF@line$($line + 1)"
            $problems += "every tick line matches, but the reports differ at line $($line + 1) ($($bytesA.Length) and $($bytesB.Length) bytes)"
            if ($line -lt $lineCount) {
                $problems += "a: $textA"
                $problems += "b: $textB"
            }
        }
    }
    if ($RunA.Profile -eq 'two-cab') {
        if (($RunA.Autopilot -eq '') -or ($RunB.Autopilot -eq '')) {
            $problems += "the autopilot summary line is missing from $(@(@($RunA, $RunB) | Where-Object { $_.Autopilot -eq '' } | ForEach-Object { $_.StdoutPath }) -join ' and ')"
            if ($identity -eq 'a==b') {
                $identity = 'DIFF@finish'
            }
        }
        elseif (-not [string]::Equals($RunA.Autopilot, $RunB.Autopilot, [System.StringComparison]::Ordinal)) {
            $problems += 'the autopilot summary lines differ'
            $problems += "a: $($RunA.Autopilot)"
            $problems += "b: $($RunB.Autopilot)"
            if ($identity -eq 'a==b') {
                $identity = 'DIFF@finish'
            }
        }
    }
    $RunA.Identity = $identity
    $RunB.Identity = $identity
    return $problems
}

try {
    # The planned tracks: -Tracks (each a table levelID), or the whole table.
    $plannedTracks = @()
    if ($PSBoundParameters.ContainsKey('Tracks')) {
        foreach ($item in $Tracks) {
            foreach ($text in ($item -split '[,\s]+')) {
                if ($text -eq '') {
                    continue
                }
                if ($text -notmatch '^[0-9]+$') {
                    Exit-Failed "invalid track '$text' (a levelID)"
                }
                $id = [int]$text
                $row = @($trackTable | Where-Object { $_.Id -eq $id })
                if ($row.Count -ne 1) {
                    Exit-Failed "track $id is not one of the 16 arcade match-select tracks ($(($trackTable | ForEach-Object { $_.Id }) -join ' '))"
                }
                if (@($plannedTracks | Where-Object { $_.Id -eq $id }).Count -ne 0) {
                    Exit-Failed "track $id is listed twice"
                }
                $plannedTracks += $row[0]
            }
        }
        if ($plannedTracks.Count -eq 0) {
            Exit-Failed '-Tracks names no track'
        }
    }
    else {
        $plannedTracks = $trackTable
    }
    $plannedProfiles = @($Profile)
    if ($Profile -eq 'both') {
        $plannedProfiles = @('two-cab', 'one-cab')
    }
    foreach ($name in $plannedProfiles) {
        $limit = $profileTable[$name].MaxTicks
        if (($Ticks -lt 1) -or ($Ticks -gt $limit)) {
            Exit-Failed "invalid tick count $Ticks for $name (1..$limit)"
        }
    }
    # The paired profiles: each must be one -Profile runs.
    $pairedProfiles = @()
    if ($Pairs -eq 'two-cab') {
        $pairedProfiles = @('two-cab')
    }
    elseif ($Pairs -eq 'both') {
        $pairedProfiles = @('two-cab', 'one-cab')
    }
    foreach ($name in $pairedProfiles) {
        if ($plannedProfiles -notcontains $name) {
            Exit-Failed "-Pairs $Pairs pairs $name, which -Profile $Profile does not run"
        }
    }
    if ($Parallel -lt 1) {
        Exit-Failed "invalid -Parallel $Parallel (at least 1)"
    }
    if ($TimeoutSeconds -lt 1) {
        Exit-Failed "invalid -TimeoutSeconds $TimeoutSeconds (at least 1)"
    }
    if (-not [System.IO.Path]::IsPathRooted($OutputDirectory)) {
        Exit-Failed "OutputDirectory must be an absolute path: $OutputDirectory"
    }
    $resolvedOutput = [System.IO.Path]::GetFullPath($OutputDirectory)

    # One run per (track, profile), track-major; a paired profile's run b
    # right after its run a, with the same arguments but its own report path.
    $pairList = @()
    foreach ($track in $plannedTracks) {
        foreach ($name in $plannedProfiles) {
            $members = @('-')
            if ($pairedProfiles -contains $name) {
                $members = @('a', 'b')
            }
            $pairRuns = @()
            foreach ($member in $members) {
                $directoryName = 'track{0:D2}-{1}' -f $track.Id, $name
                if ($member -eq 'b') {
                    $directoryName += '-b'
                }
                $directory = Join-Path $resolvedOutput $directoryName
                $reportPath = Join-Path $directory 'report.txt'
                $arguments = @('--arcade-roster-proof', $reportPath, '--arcade-roster-proof-seed', $seed,
                    '--arcade-roster-proof-dwell', "$dwell", '--arcade-roster-proof-ticks', "$Ticks") +
                    $profileTable[$name].Flags + @('--arcade-roster-proof-track', "$($track.Id)")
                $run = [pscustomobject]@{
                    Track = $track.Id
                    TrackName = $track.Name
                    Profile = $name
                    ReportProfile = $profileTable[$name].Report
                    Pair = $member
                    Arguments = $arguments
                    Directory = $directory
                    ReportPath = $reportPath
                    StdoutPath = Join-Path $directory 'stdout.log'
                    StderrPath = Join-Path $directory 'stderr.log'
                    Process = $null
                    StartedAt = $null
                    Seconds = $null
                    TimedOut = $false
                    Done = $false
                    Result = ''
                    ResultLine = ''
                    Reached = $null
                    Problems = @()
                    Autopilot = ''
                    EndOfRace = '-'
                    FinishP0 = '-'
                    FinishP1 = '-'
                    Identity = '-'
                }
                $runs += $run
                $pairRuns += $run
            }
            if ($pairRuns.Count -eq 2) {
                $pairList += [pscustomobject]@{ A = $pairRuns[0]; B = $pairRuns[1]; Compared = $false; Problems = @() }
            }
        }
    }

    if ($ListRuns) {
        $header = "sweep ticks $Ticks profile $Profile parallel $Parallel runs $($runs.Count)"
        if ($Pairs -ne 'none') {
            $header += " pairs $Pairs $($pairList.Count)"
        }
        Write-Output $header
        foreach ($run in $runs) {
            $member = ''
            if ($run.Pair -ne '-') {
                $member = " pair $($run.Pair)"
            }
            Write-Output "run track $($run.Track) ($($run.TrackName)) profile $($run.Profile)$($member): $(($run.Arguments | ForEach-Object { ConvertTo-ProcessArgument $_ }) -join ' ')"
        }
        exit 0
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
    $resolvedExecutable = (Resolve-Path -LiteralPath $Executable -ErrorAction Stop).Path
    [System.IO.Directory]::CreateDirectory($resolvedOutput) | Out-Null

    Write-Output "arcade roster track sweep: $($runs.Count) runs ($($plannedTracks.Count) tracks x $($plannedProfiles -join ' + ')), $Ticks race ticks each, $Parallel at a time, seed $seed, dwell $dwell"
    Write-Output "executable: $resolvedExecutable"
    Write-Output "output:     $resolvedOutput"

    $sweepStartedAt = Get-Date
    $queue = New-Object System.Collections.Queue
    foreach ($run in $runs) {
        $queue.Enqueue($run)
    }
    $active = @()
    while (($queue.Count -gt 0) -or ($active.Count -gt 0)) {
        while (($queue.Count -gt 0) -and ($active.Count -lt $Parallel)) {
            $run = $queue.Dequeue()
            Start-SweepRun $run
            $active += $run
        }
        Start-Sleep -Milliseconds 500
        $stillActive = @()
        foreach ($run in $active) {
            if (-not $run.Process.HasExited) {
                if (((Get-Date) - $run.StartedAt).TotalSeconds -gt $TimeoutSeconds) {
                    $run.TimedOut = $true
                    Stop-StartedRuns @($run)
                }
                else {
                    $stillActive += $run
                    continue
                }
            }
            $run.Process.WaitForExit()
            $endedAt = Get-Date
            if (-not $run.TimedOut) {
                $endedAt = $run.Process.ExitTime
            }
            $run.Seconds = [math]::Round(($endedAt - $run.StartedAt).TotalSeconds, 1)
            # A missing display or a non-internal build is known as soon as
            # one run exits, so the rest are stopped early.
            $log = (Read-SharedText $run.StdoutPath) + "`n" + (Read-SharedText $run.StderrPath)
            if ($log.Contains($noDisplayMarker)) {
                Stop-StartedRuns $runs
                Exit-Skipped "no display available to SDL (track $($run.Track) $($run.Profile), see $($run.StderrPath)); a desktop session is required"
            }
            if ($log.Contains($notInternalMarker)) {
                Stop-StartedRuns $runs
                Exit-Skipped "--arcade-roster-proof was rejected: the executable is not an internal (CTR_INTERNAL) build (see $($run.StderrPath))"
            }
            Test-SweepRun $run
            Read-AutopilotSummary $run
            $run.Done = $true
            $status = 'ok'
            if ($run.Problems.Count -ne 0) {
                $status = 'FAIL'
            }
            $label = $run.Profile
            if ($run.Pair -ne '-') {
                $label += " $($run.Pair)"
            }
            Write-Output ("done track {0,2} {1,-7} {2} in {3} s ({4})" -f $run.Track, $label, $run.Result, $run.Seconds, $status)
            if ($run.Problems.Count -ne 0) {
                foreach ($problem in $run.Problems) {
                    Write-Output "  problem: $problem"
                }
                Write-Output "  result line: $($run.ResultLine)"
                Write-Output "  stdout tail ($($run.StdoutPath)):"
                $tail = @((Read-SharedText $run.StdoutPath) -split "`r?`n" | Where-Object { $_ -ne '' })
                $first = [math]::Max(0, $tail.Count - 20)
                for ($i = $first; $i -lt $tail.Count; $i++) {
                    Write-Output "    $($tail[$i])"
                }
            }
            # Both runs of a pair done: compare them (only when both passed).
            foreach ($pair in @($pairList | Where-Object { (-not $_.Compared) -and $_.A.Done -and $_.B.Done })) {
                $pair.Compared = $true
                $pairName = "pair track $($pair.A.Track) $($pair.A.Profile)"
                if (($pair.A.Problems.Count -ne 0) -or ($pair.B.Problems.Count -ne 0)) {
                    $pair.A.Identity = 'n/a'
                    $pair.B.Identity = 'n/a'
                    $pair.Problems = @('not compared: a run of the pair failed')
                    Write-Output "$($pairName): not compared (a run failed)"
                    continue
                }
                $pair.Problems = @(Compare-SweepPair $pair.A $pair.B)
                $finish = ''
                if ($pair.A.Profile -eq 'two-cab') {
                    $finish = " (END_OF_RACE $($pair.A.EndOfRace) p0 $($pair.A.FinishP0) p1 $($pair.A.FinishP1))"
                }
                if ($pair.Problems.Count -eq 0) {
                    Write-Output "$($pairName): a==b, byte-identical reports$finish"
                }
                else {
                    Write-Output "$($pairName): $($pair.A.Identity) (FAIL)"
                    foreach ($problem in $pair.Problems) {
                        Write-Output "  $problem"
                    }
                }
            }
        }
        $active = $stillActive
    }
    $totalSeconds = [math]::Round(((Get-Date) - $sweepStartedAt).TotalSeconds, 1)

    Write-Output ''
    $rowFormat = '{0,5}  {1,-16} {2,-7} {3,-4}  {4,-20} {5,11}  {6,-12} {7,6} {8,6} {9,6} {10,8}'
    Write-Output ($rowFormat -f 'track', 'name', 'profile', 'pair', 'result', 'race ticks', 'identity', 'EOR', 'p0 fin', 'p1 fin', 'seconds')
    $failed = @()
    foreach ($run in $runs) {
        $reached = '-'
        if ($null -ne $run.Reached) {
            $reached = "$($run.Reached)/$Ticks"
        }
        $result = $run.Result
        if (($run.Problems.Count -ne 0) -and ($result -eq 'PASS')) {
            $result = 'PASS (bad report)'
        }
        Write-Output ($rowFormat -f $run.Track, $run.TrackName, $run.Profile, $run.Pair, $result, $reached, $run.Identity,
            $run.EndOfRace, $run.FinishP0, $run.FinishP1, $run.Seconds)
        if ($run.Problems.Count -ne 0) {
            $failed += $run
        }
    }
    $failedPairs = @($pairList | Where-Object { $_.Problems.Count -ne 0 })
    Write-Output ''
    Write-Output "all runs done in $totalSeconds s"
    $pairCounts = ''
    if ($pairList.Count -ne 0) {
        $pairCounts = ", $($pairList.Count - $failedPairs.Count) of $($pairList.Count) pairs byte-identical"
    }
    if (($failed.Count -ne 0) -or ($failedPairs.Count -ne 0)) {
        foreach ($run in $failed) {
            Write-Output "FAIL: track $($run.Track) ($($run.TrackName)) $($run.Profile) $($run.Pair): $($run.Problems -join '; ') (logs in $($run.Directory))"
        }
        foreach ($pair in $failedPairs) {
            Write-Output "FAIL: pair track $($pair.A.Track) ($($pair.A.TrackName)) $($pair.A.Profile) $($pair.A.Identity): $($pair.Problems -join '; ') (reports $($pair.A.ReportPath) and $($pair.B.ReportPath))"
        }
        Write-Output "arcade roster track sweep: FAIL ($($failed.Count) of $($runs.Count) runs failed$pairCounts)"
        exit 1
    }
    Write-Output "arcade roster track sweep: PASS ($($runs.Count) runs$pairCounts)"
    exit 0
}
finally {
    # On every exit and on an interrupt: no ctr_native this sweep started
    # outlives it.
    Stop-StartedRuns $runs
}
