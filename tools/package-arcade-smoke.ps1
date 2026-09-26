<#
Package smoke gate (docs/PACKAGING.md "Package smoke gate"): proves that a
PACKAGED ctr_native.exe runs a two-process loopback lockstep race driven by
the package's own config files. Windows PowerShell 5.1 compatible.

  tools/package-arcade-smoke.ps1 -PackageDirectory <dir> -OutputDirectory <dir>
      Smoke-tests an existing package folder (for example the one
      tools/package-arcade.ps1 printed). The folder is only read.

  tools/package-arcade-smoke.ps1 -StageExecutable <exe> -OutputDirectory <dir>
      Stages a test package (ctr-arcade-staged) from <exe> into
      <output>\package with tools/package-arcade.ps1 -StageExecutable, then
      smoke-tests it (the ctest package_arcade_smoke).

Steps: the retail-data guard on the package folder and every MANIFEST.txt
size and SHA-256 against the files; a copy of the package into a fresh
<output>\run (the game writes its log and memcards\ next to the exe, so the
package folder itself is never run); the copied files re-checked against the
MANIFEST, and asserted to hold no memcards\ (a fresh cabinet, no memcard
save); <output>\cab1.loopback.cfg and cab2.loopback.cfg derived from the
package's cab1.cfg and cab2.cfg by changing ONLY the peer IP (to 127.0.0.1,
ports kept), data_dir (to the folder holding -AssetsFile), and fullscreen
(to 0); then tools/arcade-link-launch-check.ps1 on <output>\run\ctr_native.exe
with -Cab1Config and -Cab2Config, output in <output>\gate.

-OutputDirectory must resolve under <repo>\build-msvc-x86\. Exit codes: 0
pass, 1 fail, 77 skipped (no disc image, or the gate skipped: no display, a
non-internal build, or an unknown build identity). A skip is not a pass.
#>
[CmdletBinding()]
param(
    # An existing package folder (exactly one of this and -StageExecutable).
    [string]$PackageDirectory,

    # The exe to stage as a test package (exactly one of this and
    # -PackageDirectory).
    [string]$StageExecutable,

    # Work folder under <repo>\build-msvc-x86\: package\ (stage mode), run\,
    # gate\, and the two loopback configs.
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    # User-supplied disc image; the smoke skips (77) when it is absent.
    # Defaults to <repo>\assets\ctr-u.bin, resolved below: Windows PowerShell
    # 5.1 leaves $PSScriptRoot empty inside the param defaults of a
    # [CmdletBinding()] script run with -File.
    [string]$AssetsFile,

    # Passed to the gate: seconds both runs together may take.
    [int]$TimeoutSeconds = 780
)

$ErrorActionPreference = 'Stop'
$skipExitCode = 77
$startedAt = Get-Date
# The files MANIFEST.txt lists (every package file but itself).
$manifestFiles = @('ctr_native.exe', 'cab1.cfg', 'cab2.cfg', 'README.txt')
$packageFiles = $manifestFiles + @('MANIFEST.txt')
# The seats, ports, and loopback peers of the package's cab1.cfg and cab2.cfg
# (the real cabinet ports, also tools/arcade-link-launch-check.ps1's default
# -Cab1Port and -Cab2Port); the loopback configs must hold the same.  The
# ctest arcade_link_launch runs the gate on another port pair, so it can
# overlap this smoke.
$cabs = @(
    @{ Name = 'cab1'; Seat = 'cab1'; Port = '7001'; PeerPort = '7002' },
    @{ Name = 'cab2'; Seat = 'cab2'; Port = '7002'; PeerPort = '7001' })
$loopbackIp = '127.0.0.1'
$loopbackFullscreen = '0'

# Write-Host, not Write-Output: these also run inside functions whose output
# is captured, and the reason must still reach the console.
function Exit-Failed([string]$Reason) {
    Write-Host "FAIL: $Reason"
    Write-Host 'package smoke: FAIL'
    exit 1
}

function Exit-Skipped([string]$Reason) {
    Write-Host "SKIPPED: $Reason"
    Write-Host 'package smoke: SKIPPED'
    exit $skipExitCode
}

function Get-FullPath([string]$Path) {
    $provider = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
    return [System.IO.Path]::GetFullPath($provider).TrimEnd('\')
}

function Test-UnderPath([string]$Path, [string]$Root) {
    return $Path.StartsWith($Root.TrimEnd('\') + '\', [System.StringComparison]::OrdinalIgnoreCase)
}

function Test-SameOrUnderPath([string]$Path, [string]$Root) {
    return ($Path.TrimEnd('\') -ieq $Root.TrimEnd('\')) -or (Test-UnderPath $Path $Root)
}

# Never write or delete through a junction or symlink: checks every existing
# folder from build-msvc-x86 down to $Path.
function Assert-NoReparsePoint([string]$Path) {
    $walk = $Path.TrimEnd('\')
    while ($walk.Length -ge $buildDir.Length) {
        if (Test-Path -LiteralPath $walk) {
            $item = Get-Item -LiteralPath $walk -Force
            if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                Exit-Failed "refusing to write through a junction or symbolic link: $walk"
            }
        }
        if ($walk.Length -eq $buildDir.Length) {
            break
        }
        $walk = Split-Path -Parent $walk
    }
}

# Parses MANIFEST.txt's file lines ("name  size  SHA-256") into a hashtable
# name -> { Size; Hash }. The listed names must be exactly $manifestFiles.
function Read-Manifest([string]$Folder) {
    $manifestPath = Join-Path $Folder 'MANIFEST.txt'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        Exit-Failed "no MANIFEST.txt in $Folder"
    }
    $lines = @([System.IO.File]::ReadAllLines($manifestPath))
    $header = [array]::IndexOf($lines, 'files (name, size in bytes, SHA-256):')
    if ($header -lt 0) {
        Exit-Failed "MANIFEST.txt in $Folder has no 'files (name, size in bytes, SHA-256):' line"
    }
    $entries = @{}
    for ($i = $header + 1; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]
        if ($line -eq '') {
            continue
        }
        if ($line -cnotmatch '^(\S+)  ([0-9]+)  ([0-9A-F]{64})$') {
            Exit-Failed "MANIFEST.txt in $Folder line $($i + 1) is malformed: '$line'"
        }
        $name = $Matches[1]
        if ($manifestFiles -cnotcontains $name) {
            Exit-Failed "MANIFEST.txt in $Folder lists an unexpected file '$name'"
        }
        if ($entries.ContainsKey($name)) {
            Exit-Failed "MANIFEST.txt in $Folder lists '$name' twice"
        }
        $entries[$name] = [pscustomobject]@{ Size = [long]$Matches[2]; Hash = $Matches[3] }
    }
    foreach ($name in $manifestFiles) {
        if (-not $entries.ContainsKey($name)) {
            Exit-Failed "MANIFEST.txt in $Folder does not list '$name'"
        }
    }
    return $entries
}

# Checks the size and SHA-256 of every listed file in $Folder against
# $Manifest; prints one line per file.
function Assert-ManifestMatches([string]$Folder, $Manifest, [string]$What) {
    foreach ($name in $manifestFiles) {
        $path = Join-Path $Folder $name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            Exit-Failed "${What}: $name is missing from $Folder"
        }
        $size = (Get-Item -LiteralPath $path -Force).Length
        $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        $expected = $Manifest[$name]
        if (($size -ne $expected.Size) -or ($hash -cne $expected.Hash)) {
            Exit-Failed "${What}: $path is $size bytes SHA-256 $hash; MANIFEST.txt says $($expected.Size) bytes SHA-256 $($expected.Hash)"
        }
        Write-Host "${What}: $name  $size  $hash  matches MANIFEST.txt"
    }
}

function Test-ConfigComment([string]$Line) {
    $trimmed = $Line.Trim(" `t")
    return ($trimmed -eq '') -or $trimmed.StartsWith('#') -or $trimmed.StartsWith(';')
}

# Splits a non-comment config line at its first '=' (docs/PACKAGING.md
# config grammar); $null when there is none.
function Split-ConfigLine([string]$Line) {
    $equals = $Line.IndexOf('=')
    if ($equals -lt 0) {
        return $null
    }
    return [pscustomobject]@{
        Key = $Line.Substring(0, $equals).Trim(" `t")
        Value = $Line.Substring($equals + 1).Trim(" `t")
    }
}

function Split-ConfigText([string]$Text) {
    return , ([regex]::Split($Text, "`r?`n"))
}

# The loopback value of a key the smoke changes, or $null for a key it keeps.
function Get-LoopbackValue($Cab, [string]$Key) {
    if ($Key -ceq 'peer') {
        return "${loopbackIp}:$($Cab.PeerPort)"
    }
    if ($Key -ceq 'data_dir') {
        return $dataDir
    }
    if ($Key -ceq 'fullscreen') {
        return $loopbackFullscreen
    }
    return $null
}

# Writes $Destination from the package's $Source, changing only the peer IP,
# data_dir, and fullscreen. seat, port, and the peer port must already be
# the gate's; each of the five keys must appear exactly once.
function New-LoopbackConfig($Cab, [string]$Source, [string]$Destination) {
    $text = [System.IO.File]::ReadAllText($Source)
    $newline = "`n"
    if ($text.Contains("`r`n")) {
        $newline = "`r`n"
    }
    $lines = Split-ConfigText $text
    $counts = @{ 'data_dir' = 0; 'seat' = 0; 'port' = 0; 'peer' = 0; 'fullscreen' = 0 }
    $derivedLines = New-Object System.Collections.Generic.List[string]
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]
        if (Test-ConfigComment $line) {
            $derivedLines.Add($line)
            continue
        }
        $entry = Split-ConfigLine $line
        if ($null -eq $entry) {
            Exit-Failed "$Source line $($i + 1) has no '=': '$line'"
        }
        if ($counts.ContainsKey($entry.Key)) {
            $counts[$entry.Key]++
        }
        if ($entry.Key -ceq 'seat') {
            if ($entry.Value -cne $Cab.Seat) {
                Exit-Failed "$Source line $($i + 1): seat is '$($entry.Value)', the gate's $($Cab.Name) is '$($Cab.Seat)'"
            }
        }
        elseif ($entry.Key -ceq 'port') {
            if ($entry.Value -cne $Cab.Port) {
                Exit-Failed "$Source line $($i + 1): port is '$($entry.Value)', the gate's $($Cab.Name) uses $($Cab.Port)"
            }
        }
        elseif ($entry.Key -ceq 'peer') {
            if ($entry.Value -cnotmatch '^[0-9]{1,3}(\.[0-9]{1,3}){3}:([0-9]+)$') {
                Exit-Failed "$Source line $($i + 1): peer '$($entry.Value)' is not a.b.c.d:port"
            }
            if ($Matches[2] -cne $Cab.PeerPort) {
                Exit-Failed "$Source line $($i + 1): the peer port is $($Matches[2]), the gate's $($Cab.Name) peer port is $($Cab.PeerPort)"
            }
        }
        $loopbackValue = Get-LoopbackValue $Cab $entry.Key
        if ($null -eq $loopbackValue) {
            $derivedLines.Add($line)
        }
        else {
            $derivedLines.Add("$($entry.Key) = $loopbackValue")
        }
    }
    foreach ($key in @('data_dir', 'seat', 'port', 'peer', 'fullscreen')) {
        if ($counts[$key] -ne 1) {
            Exit-Failed "$Source has $($counts[$key]) '$key' line(s), expected exactly 1"
        }
    }
    [System.IO.File]::WriteAllText($Destination, ($derivedLines -join $newline), (New-Object System.Text.UTF8Encoding($false)))
}

# Re-reads both files: the same line count, every comment and every other
# non-comment line byte-equal, and peer, data_dir, and fullscreen holding
# exactly their loopback values.
function Assert-LoopbackConfig($Cab, [string]$Source, [string]$Derived) {
    $sourceLines = Split-ConfigText ([System.IO.File]::ReadAllText($Source))
    $derivedLines = Split-ConfigText ([System.IO.File]::ReadAllText($Derived))
    if ($sourceLines.Count -ne $derivedLines.Count) {
        Exit-Failed "$Derived has $($derivedLines.Count) lines, $Source has $($sourceLines.Count)"
    }
    $changed = 0
    for ($i = 0; $i -lt $sourceLines.Count; $i++) {
        $sourceLine = $sourceLines[$i]
        $derivedLine = $derivedLines[$i]
        $loopbackValue = $null
        if (-not (Test-ConfigComment $sourceLine)) {
            $loopbackValue = Get-LoopbackValue $Cab (Split-ConfigLine $sourceLine).Key
        }
        if ($null -eq $loopbackValue) {
            if ($derivedLine -cne $sourceLine) {
                Exit-Failed "$Derived line $($i + 1) is '$derivedLine', expected it unchanged from $Source ('$sourceLine')"
            }
            continue
        }
        $sourceEntry = Split-ConfigLine $sourceLine
        $derivedEntry = Split-ConfigLine $derivedLine
        if (($null -eq $derivedEntry) -or ($derivedEntry.Key -cne $sourceEntry.Key) -or ($derivedEntry.Value -cne $loopbackValue)) {
            Exit-Failed "$Derived line $($i + 1) is '$derivedLine', expected '$($sourceEntry.Key) = $loopbackValue'"
        }
        $changed++
    }
    if ($changed -ne 3) {
        Exit-Failed "$Derived changes $changed lines of $Source, expected 3 (peer, data_dir, fullscreen)"
    }
}

# ---------------------------------------------------------------------------
# Arguments.

$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildDir = (Join-Path $repo 'build-msvc-x86').TrimEnd('\')
$packageScript = Join-Path $PSScriptRoot 'package-arcade.ps1'
$gateScript = Join-Path $PSScriptRoot 'arcade-link-launch-check.ps1'
# The PowerShell running this script runs the two helpers too.
$powershellExe = (Get-Process -Id $PID).Path

$packageGiven = -not [string]::IsNullOrWhiteSpace($PackageDirectory)
$stageGiven = -not [string]::IsNullOrWhiteSpace($StageExecutable)
if ($packageGiven -eq $stageGiven) {
    Exit-Failed 'give exactly one of -PackageDirectory and -StageExecutable'
}
if ($TimeoutSeconds -lt 1) {
    Exit-Failed "invalid timeout $TimeoutSeconds s"
}
$output = Get-FullPath $OutputDirectory
if (-not (Test-UnderPath $output $buildDir)) {
    Exit-Failed "refusing output directory outside $buildDir\: $output"
}
if (Test-SameOrUnderPath $output 'C:\arcade') {
    Exit-Failed "refusing output directory under C:\arcade: $output"
}
$runDir = Join-Path $output 'run'
$gateDir = Join-Path $output 'gate'
$stageDir = Join-Path $output 'package'

if ($packageGiven) {
    $packageDir = Get-FullPath $PackageDirectory
    if (-not (Test-Path -LiteralPath $packageDir -PathType Container)) {
        Exit-Failed "package folder not found: $packageDir"
    }
    # The package is only read: it must not be one of the folders this
    # smoke recreates or writes, nor hold them.
    if ((Test-SameOrUnderPath $output $packageDir) -or (Test-SameOrUnderPath $packageDir $runDir) -or
        (Test-SameOrUnderPath $packageDir $gateDir)) {
        Exit-Failed "the package folder $packageDir and the output directory $output overlap"
    }
}
else {
    $packageDir = $stageDir
}

if ([string]::IsNullOrWhiteSpace($AssetsFile)) {
    $AssetsFile = Join-Path $repo 'assets\ctr-u.bin'
}
$AssetsFile = Get-FullPath $AssetsFile
if (-not (Test-Path -LiteralPath $AssetsFile -PathType Leaf)) {
    Exit-Skipped "assets file not found: $AssetsFile (user-supplied disc image required)"
}
$dataDir = Split-Path -Parent $AssetsFile

Assert-NoReparsePoint $output
Assert-NoReparsePoint $runDir
Assert-NoReparsePoint $gateDir
[System.IO.Directory]::CreateDirectory($output) | Out-Null

Write-Output 'package smoke: a packaged ctr_native.exe, run from a copy of its package folder, in the two-process loopback gate driven by the package''s own config files'
Write-Output "output: $output"

# ---------------------------------------------------------------------------
# a. Stage (stage mode only).

if ($stageGiven) {
    Write-Output ''
    $stageExe = Get-FullPath $StageExecutable
    Write-Output "stage: $stageExe -> $stageDir"
    & $powershellExe -NoProfile -ExecutionPolicy Bypass -File $packageScript -StageExecutable $stageExe -StageDirectory $stageDir
    if ($LASTEXITCODE -ne 0) {
        Exit-Failed "tools/package-arcade.ps1 -StageExecutable failed ($LASTEXITCODE)"
    }
}

# ---------------------------------------------------------------------------
# b. The guard and the MANIFEST.

Write-Output ''
Write-Output "package: $packageDir"
& $powershellExe -NoProfile -ExecutionPolicy Bypass -File $packageScript -CheckFolder $packageDir
if ($LASTEXITCODE -ne 0) {
    Exit-Failed "the package folder failed the retail-data guard ($LASTEXITCODE)"
}
$manifest = Read-Manifest $packageDir
foreach ($line in @([System.IO.File]::ReadAllLines((Join-Path $packageDir 'MANIFEST.txt')))) {
    if ($line -match '^(package|version|commit|STAGED TEST PACKAGE): ') {
        Write-Output "  MANIFEST $line"
    }
}
Assert-ManifestMatches $packageDir $manifest 'package'

# ---------------------------------------------------------------------------
# c. A fresh copy to run from: the game writes its log and memcards\ into the
# exe folder, and the package folder must stay untouched.

if (Test-Path -LiteralPath $runDir) {
    Remove-Item -LiteralPath $runDir -Recurse -Force
}
[System.IO.Directory]::CreateDirectory($runDir) | Out-Null
foreach ($name in $packageFiles) {
    Copy-Item -LiteralPath (Join-Path $packageDir $name) -Destination (Join-Path $runDir $name)
}
Write-Output ''
Write-Output "run copy: $runDir"
Assert-ManifestMatches $runDir $manifest 'run copy'
$runExe = Join-Path $runDir 'ctr_native.exe'

# ---------------------------------------------------------------------------
# d. The loopback configs.

Write-Output ''
$configs = @{}
foreach ($cab in $cabs) {
    $source = Join-Path $packageDir "$($cab.Name).cfg"
    $derived = Join-Path $output "$($cab.Name).loopback.cfg"
    New-LoopbackConfig $cab $source $derived
    Assert-LoopbackConfig $cab $source $derived
    $configs[$cab.Name] = $derived
    Write-Output ("loopback config {0}: {1} (seat {2}, port {3}, peer {4}:{5}, data_dir {6}, fullscreen {7}; every other line as in the package's {0}.cfg)" -f
        $cab.Name, $derived, $cab.Seat, $cab.Port, $loopbackIp, $cab.PeerPort, $dataDir, $loopbackFullscreen)
}

# ---------------------------------------------------------------------------
# e. The gate, on a fresh cabinet: the run copy must hold no memcards\ (no
# memcard save, so no options were ever loaded from one; the race setup must
# cope with that, as on a newly installed cabinet).

Write-Output ''
$runMemcards = Join-Path $runDir 'memcards'
if (Test-Path -LiteralPath $runMemcards) {
    Exit-Failed "the run copy holds $runMemcards; the smoke must run as a fresh cabinet with no memcard save"
}
Write-Output "run copy: no memcards\ folder in $runDir (a fresh cabinet with no memcard save)"
& $powershellExe -NoProfile -ExecutionPolicy Bypass -File $gateScript -Executable $runExe -Cab1Config $configs['cab1'] `
    -Cab2Config $configs['cab2'] -OutputDirectory $gateDir -AssetsFile $AssetsFile -TimeoutSeconds $TimeoutSeconds
$gateExit = $LASTEXITCODE
Write-Output ''
if ($gateExit -eq $skipExitCode) {
    Exit-Skipped 'tools/arcade-link-launch-check.ps1 skipped (see its SKIPPED line above)'
}
if ($gateExit -ne 0) {
    Exit-Failed "tools/arcade-link-launch-check.ps1 exited $gateExit"
}

# ---------------------------------------------------------------------------
# f. After the pass: every config group came from the file (the window mode
# too), the exe that ran and the package are still the MANIFEST's.

foreach ($cab in $cabs) {
    $stdoutPath = Join-Path $gateDir "$($cab.Name).stdout.log"
    $stdoutLines = @([System.IO.File]::ReadAllLines($stdoutPath))
    foreach ($expected in @('[CTR Native] Config groups from the file: link fullscreen data_dir', '[CTR Native] Local window mode: windowed')) {
        $hits = @($stdoutLines | Where-Object { $_ -ceq $expected })
        if ($hits.Count -ne 1) {
            Exit-Failed "$stdoutPath has $($hits.Count) '$expected' line(s), expected 1"
        }
    }
    Write-Output "$($cab.Name): link, fullscreen (windowed), and data_dir from $($configs[$cab.Name])"
}
Assert-ManifestMatches $runDir $manifest 'after the run, run copy'
Assert-ManifestMatches $packageDir $manifest 'after the run, package'

$exeItem = Get-Item -LiteralPath $runExe
$exeHash = (Get-FileHash -LiteralPath $runExe -Algorithm SHA256).Hash
Write-Output ''
Write-Output "package: $packageDir"
Write-Output "ctr_native.exe SHA-256: $exeHash"
Write-Output "ctr_native.exe size: $($exeItem.Length) bytes"
Write-Output ("package smoke: elapsed {0} s" -f [math]::Round(((Get-Date) - $startedAt).TotalSeconds, 1))
Write-Output 'package smoke: PASS'
exit 0
