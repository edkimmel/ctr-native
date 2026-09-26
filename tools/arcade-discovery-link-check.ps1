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

    # Optional config files (docs/PACKAGING.md PK-2), both or neither: when
    # given, run a gets --config <ConfigA> and run b --config <ConfigB> in
    # place of --arcade-link auto --arcade-link-port <port>, so each link
    # group comes from its file.  The package smoke gate
    # (tools/package-arcade-smoke.ps1) passes two loopback copies of the
    # package's arcade.cfg.
    [string]$ConfigA,
    [string]$ConfigB,

    # The loopback link ports of runs a and b, and their discovery ports
    # (each run beacons at the other's).  The defaults are the ctest
    # arcade_discovery_link's; the package smoke passes 7001-7004.  With
    # -ConfigA/-ConfigB the link ports must be the ones the files hold: the
    # pairing check expects them.  All four must differ.
    [int]$LinkPortA = 7301,
    [int]$LinkPortB = 7302,
    [int]$DiscoveryPortA = 7303,
    [int]$DiscoveryPortB = 7304,

    # Seconds both runs together may take.
    [int]$TimeoutSeconds = 180
)

# Discovery live link gate (docs/DISCOVERY_MILESTONE.md DISC-12, slice
# DISC-S4).  Starts two internal ctr_native processes at once in discovery
# mode: no peer and no fixed seat, each beaconing at the other's discovery
# port on loopback.  By default (the ctest arcade_discovery_link) on ports
# outside 7001-7004 (package_arcade_smoke), 7101/7102 (arcade_link_launch),
# 7201-7204 (arcade_solo_race), and 48000-48629 (the fast suite's socket
# tests):
#   a  --arcade-link auto --arcade-link-port 7301
#      --arcade-discovery-port 7303 --arcade-discovery-target 127.0.0.1:7304
#   b  --arcade-link auto --arcade-link-port 7302
#      --arcade-discovery-port 7304 --arcade-discovery-target 127.0.0.1:7303
# (the ports are -LinkPortA/-LinkPortB and -DiscoveryPortA/-DiscoveryPortB)
# each with --arcade-link-autopilot <report> --arcade-link-autopilot-one-race
# --arcade-link-autopilot-race-ticks 900, which drives the link host's own
# inputs (never a pad; include/platform/native_arcade_link_autopilot.h, the
# one-race mode): START on the attract screen, the LOBBY until the
# discovered pairing links the two, the linked select, one lockstep race,
# RESULTS, EXIT, and the title, where the autopilot passes and the process
# exits.  Both seats are auto on one IPv4, so the election (DISC-8) makes the
# lower link port cab1: by default a on 7301 cab1 and b cab2.
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
# With -ConfigA and -ConfigB (both or neither; DISC-S5), each run gets
# --config <file> in place of --arcade-link auto --arcade-link-port <port>
# (the discovery flags stay on the command line: they are not link-group
# options, DISC-11), and its stdout must also show that file loaded ("Config
# file: <path>", once), the link group taken from it, no group overridden by
# the command line, and main.c's "arcade link: auto port <link port>, 0
# peers" line once (the file's seat auto, its port, and no peer); everything
# else is unchanged.  The files must hold seat = auto, no peer, and the link
# ports given here (the package smoke gate checks its files).
#
# Skips (77) without the disc image, without a display, with a non-internal
# build (the option is rejected), or with an unknown build identity (a build
# from a dirty tree: the link cannot start).  A skip is not a pass.  Usage
# errors fail (1) before the disc image check.  Every ctr_native the check
# started is stopped when the check exits or is interrupted.  Everything is
# written under -OutputDirectory.
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
# main.c's config file startup lines (docs/PACKAGING.md PK-4), checked with
# -ConfigA/-ConfigB only.
$stdoutConfigFilePattern = '^\[CTR Native\] Config file: (.*)$'
$stdoutConfigGroupsPattern = '^\[CTR Native\] Config groups from the file:(.*)$'
$stdoutConfigOverriddenPattern = '^\[CTR Native\] Config groups overridden by the command line:(.*)$'
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
    # With a config file, its link group replaces the two link options (PK-4:
    # argv naming none, the file's group applies; the discovery flags are not
    # link-group options, DISC-11).
    $linkArguments = @('--arcade-link', 'auto', '--arcade-link-port', $Run.LinkPort)
    if ($null -ne $Run.ConfigPath) {
        $linkArguments = @('--config', $Run.ConfigPath)
    }
    $arguments = $linkArguments + @('--arcade-discovery-port', $Run.DiscoveryPort, '--arcade-discovery-target', $Run.DiscoveryTarget,
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
    if ($null -ne $Run.ConfigPath) {
        # The link group came from the file (seat auto, its link port, no
        # peer), and the command line overrode no group of it.
        $configFiles = @($lines | Where-Object { $_ -match $stdoutConfigFilePattern } | ForEach-Object { $_ -replace $stdoutConfigFilePattern, '$1' })
        if (($configFiles.Count -ne 1) -or ($configFiles[0] -cne $Run.ConfigPath)) {
            [void]$Failures.Add("${prefix}: the 'Config file:' lines on stdout are '$($configFiles -join ''', ''')', expected one '$($Run.ConfigPath)'")
        }
        $configGroups = @($lines | Where-Object { $_ -match $stdoutConfigGroupsPattern } | ForEach-Object { $_ -replace $stdoutConfigGroupsPattern, '$1' })
        if (($configGroups.Count -ne 1) -or (@($configGroups[0].Trim() -split ' +') -cnotcontains 'link')) {
            [void]$Failures.Add("${prefix}: the 'Config groups from the file:' lines on stdout are '$($configGroups -join ''', ''')', expected one naming the link group")
        }
        $configOverridden = @($lines | Where-Object { $_ -match $stdoutConfigOverriddenPattern })
        if ($configOverridden.Count -ne 0) {
            [void]$Failures.Add("${prefix}: the command line overrode config groups: '$($configOverridden -join ''', ''')'")
        }
        $expectedLinkLine = "[CTR Native] arcade link: auto port $($Run.LinkPort), 0 peers"
        $linkLines = @($lines | Where-Object { $_ -ceq $expectedLinkLine })
        if ($linkLines.Count -ne 1) {
            [void]$Failures.Add("${prefix}: $($linkLines.Count) '$expectedLinkLine' lines on stdout, expected 1 (the file's seat auto, port $($Run.LinkPort), and no peer)")
        }
    }
    Write-Output ("{0}: {1}" -f $Run.Name, (($paired | Select-Object -First 1) -replace '^\[CTR Native\] arcade discovery: ', ''))
    return $stdoutValidated
}

try {
    $checkStartedAt = Get-Date
    # The config files: both or neither, each an existing file.  The ports:
    # in range and all four distinct (one host binds them all).  A usage
    # error fails, never skips.
    $configPaths = @{}
    $configAGiven = -not [string]::IsNullOrWhiteSpace($ConfigA)
    $configBGiven = -not [string]::IsNullOrWhiteSpace($ConfigB)
    if ($configAGiven -ne $configBGiven) {
        Exit-Failed '-ConfigA and -ConfigB must be given together (or neither)'
    }
    if ($configAGiven) {
        foreach ($pair in @(@('a', $ConfigA), @('b', $ConfigB))) {
            if (-not (Test-Path -LiteralPath $pair[1] -PathType Leaf)) {
                Exit-Failed "config file for run $($pair[0]) not found: $($pair[1])"
            }
            $configPaths[$pair[0]] = (Resolve-Path -LiteralPath $pair[1] -ErrorAction Stop).ProviderPath
        }
    }
    $ports = @($LinkPortA, $LinkPortB, $DiscoveryPortA, $DiscoveryPortB)
    foreach ($port in $ports) {
        if (($port -lt 1) -or ($port -gt 65535)) {
            Exit-Failed "invalid port $port (1..65535)"
        }
    }
    if (@($ports | Sort-Object -Unique).Count -ne $ports.Count) {
        Exit-Failed "-LinkPortA, -LinkPortB, -DiscoveryPortA, and -DiscoveryPortB must all differ ($($ports -join ', '))"
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
    if ($TimeoutSeconds -lt 1) {
        Exit-Failed "invalid timeout $TimeoutSeconds s"
    }
    $resolvedExecutable = (Resolve-Path -LiteralPath $Executable -ErrorAction Stop).Path
    $resolvedOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
    [System.IO.Directory]::CreateDirectory($resolvedOutput) | Out-Null

    # The election (DISC-8): both seats auto on one IPv4, so the lower link
    # port is cab1.
    $cabA = 1
    $cabB = 2
    if ($LinkPortA -gt $LinkPortB) {
        $cabA = 2
        $cabB = 1
    }
    $specs = @(
        @{ Name = 'a'; CabNumber = $cabA; LinkPort = "$LinkPortA"; PeerLinkPort = "$LinkPortB"; DiscoveryPort = "$DiscoveryPortA"; DiscoveryTarget = "127.0.0.1:$DiscoveryPortB" },
        @{ Name = 'b'; CabNumber = $cabB; LinkPort = "$LinkPortB"; PeerLinkPort = "$LinkPortA"; DiscoveryPort = "$DiscoveryPortB"; DiscoveryTarget = "127.0.0.1:$DiscoveryPortA" })
    foreach ($spec in $specs) {
        $run = [pscustomobject]@{
            Name = $spec.Name
            CabNumber = $spec.CabNumber
            LinkPort = $spec.LinkPort
            PeerLinkPort = $spec.PeerLinkPort
            DiscoveryPort = $spec.DiscoveryPort
            DiscoveryTarget = $spec.DiscoveryTarget
            # $null without -ConfigA/-ConfigB: --arcade-link auto and the port.
            ConfigPath = $configPaths[$spec.Name]
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

    Write-Output "arcade discovery link check: two discovery-mode runs, seat auto, no peer (a link $LinkPortA discovery $DiscoveryPortA -> $DiscoveryPortB, b link $LinkPortB discovery $DiscoveryPortB -> $DiscoveryPortA), one linked race, race tick cap $raceTickCap"
    Write-Output "executable: $resolvedExecutable"
    Write-Output "output:     $resolvedOutput"
    if ($configAGiven) {
        Write-Output "link groups from config files: a --config $($configPaths['a']); b --config $($configPaths['b'])"
    }

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
    Write-Output "both runs: discovered each other, elected a ($LinkPortA) cab$($runs[0].CabNumber) and b ($LinkPortB) cab$($runs[1].CabNumber), linked, raced one validated race on the same match and digests to FINISHED, and exited"
    if ($configAGiven) {
        Write-Output "both runs: link group (seat auto, link port, no peer) from their config files, no group overridden by the command line"
    }
    Write-Output "arcade discovery link check: PASS"
    Write-Output ("arcade discovery link check: total time {0} s" -f [math]::Round(((Get-Date) - $checkStartedAt).TotalSeconds, 1))
    exit 0
}
finally {
    # On every exit and on an interrupt: no ctr_native this check started
    # outlives it.
    Stop-StartedRuns $runs
}
