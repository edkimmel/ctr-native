<#
Builds the arcade package (docs/PACKAGING.md PK-7): one self-contained folder
with the tested Release ctr_native.exe, the two cabinet config templates, a
README, and a MANIFEST with the SHA-256 of every file. Windows PowerShell 5.1
compatible.

  tools/package-arcade.ps1
      Requires a clean git tree, configures and builds ctr_native Release in
      build-msvc-x86, checks `ctr_native.exe --version` against HEAD, and
      writes build-msvc-x86\package\ctr-arcade-<short hash>\ (recreated).
      The destination is fixed: there is no output-path parameter, and the
      script refuses any destination outside build-msvc-x86\package\.

  tools/package-arcade.ps1 -CheckFolder <dir>
      Runs only the retail-data guard on an existing folder: exit 0 pass,
      1 fail. The folder is never changed.

  tools/package-arcade.ps1 -StageExecutable <exe> -StageDirectory <dir>
      Writes a staged TEST package (package name ctr-arcade-staged, marked
      as such in MANIFEST.txt) from the given exe into <dir> (recreated),
      through the same staging code and guard as the real package, but
      without the clean-tree, build, and --version checks. <dir> must
      resolve under build-msvc-x86\; an existing <dir> must look like a
      package folder (only allowlisted files, no subdirectory). Used by
      tools/package-arcade-smoke.ps1. Never deploy a staged package.

The guard fails a folder whose files are not exactly ctr_native.exe,
cab1.cfg, cab2.cfg, README.txt, and MANIFEST.txt (case-sensitive); that
holds any subdirectory or any hidden or system file; that holds a file named
like retail or BIOS data; whose non-exe files exceed 64 KiB; or whose exe is
32 MiB or larger.
#>
[CmdletBinding()]
param(
    # Run only the retail-data guard on this existing folder.
    [string]$CheckFolder,

    # Stage mode: the exe to stage (with -StageDirectory).
    [string]$StageExecutable,

    # Stage mode: the destination folder, under build-msvc-x86\ (with
    # -StageExecutable).
    [string]$StageDirectory
)

$ErrorActionPreference = 'Stop'

$allowedFiles = @('ctr_native.exe', 'cab1.cfg', 'cab2.cfg', 'README.txt', 'MANIFEST.txt')
$retailExtensions = @('.bin', '.cue', '.iso', '.img', '.chd', '.ecm', '.pbp', '.big', '.hwl', '.str', '.xa', '.xnf', '.vag',
    '.mcd', '.mcr', '.sav', '.srm', '.bmp', '.png')
$retailNameParts = @('bios', 'scph')
$maxSmallFileBytes = 64KB
$maxExeBytes = 32MB

# Write-Host, not Write-Output: this also runs inside functions whose output
# is captured, and the reason must still reach the console.
function Exit-Failed([string]$Reason) {
    Write-Host "FAIL: $Reason"
    exit 1
}

# Returns the list of guard violations for $Folder (empty when it passes).
function Get-GuardViolations([string]$Folder) {
    $violations = New-Object System.Collections.Generic.List[string]
    if (-not (Test-Path -LiteralPath $Folder -PathType Container)) {
        $violations.Add("not a folder: $Folder")
        return , $violations
    }
    $entries = @(Get-ChildItem -LiteralPath $Folder -Force)
    foreach ($entry in $entries) {
        if ($entry.PSIsContainer) {
            $violations.Add("subdirectory: $($entry.Name)")
            continue
        }
        $name = $entry.Name
        $lowerName = $name.ToLowerInvariant()
        if (($entry.Attributes -band ([System.IO.FileAttributes]::Hidden -bor [System.IO.FileAttributes]::System)) -ne 0) {
            $violations.Add("hidden or system file: $name")
        }
        if ($allowedFiles -cnotcontains $name) {
            $violations.Add("file not in the allowlist: $name")
        }
        if ($retailExtensions -contains [System.IO.Path]::GetExtension($lowerName)) {
            $violations.Add("retail data extension: $name")
        }
        foreach ($part in $retailNameParts) {
            if ($lowerName.Contains($part)) {
                $violations.Add("BIOS-like name ('$part'): $name")
            }
        }
        if ($name -ceq 'ctr_native.exe') {
            if ($entry.Length -ge $maxExeBytes) {
                $violations.Add("exe is $($entry.Length) bytes (limit: under $maxExeBytes)")
            }
        }
        elseif ($entry.Length -gt $maxSmallFileBytes) {
            $violations.Add("$name is $($entry.Length) bytes (limit $maxSmallFileBytes)")
        }
    }
    $names = @($entries | Where-Object { -not $_.PSIsContainer } | ForEach-Object { $_.Name })
    foreach ($required in $allowedFiles) {
        if ($names -cnotcontains $required) {
            $violations.Add("missing file: $required")
        }
    }
    return , $violations
}

function Write-GuardResult([string]$Folder, $Violations) {
    if ($Violations.Count -eq 0) {
        Write-Output "PASS: retail-data guard: $Folder"
        return
    }
    foreach ($violation in $Violations) {
        Write-Output "guard: $violation"
    }
    Write-Output "FAIL: retail-data guard: $Folder ($($Violations.Count) problem(s))"
}

$stageRequested = $PSBoundParameters.ContainsKey('StageExecutable') -or $PSBoundParameters.ContainsKey('StageDirectory')

if ($PSBoundParameters.ContainsKey('CheckFolder')) {
    if ($stageRequested) {
        Exit-Failed '-CheckFolder cannot be combined with -StageExecutable or -StageDirectory'
    }
    $violations = Get-GuardViolations $CheckFolder
    Write-GuardResult $CheckFolder $violations
    if ($violations.Count -eq 0) {
        exit 0
    }
    exit 1
}

# ---------------------------------------------------------------------------
# Package build and stage.

$repo = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$buildDir = Join-Path $repo 'build-msvc-x86'
$packageRoot = [System.IO.Path]::GetFullPath((Join-Path $buildDir 'package'))
$templates = Join-Path $repo 'tools\package'

function Get-GitOutput([string[]]$Arguments) {
    $output = & git -C $repo @Arguments
    if ($LASTEXITCODE -ne 0) {
        Exit-Failed "git $($Arguments -join ' ') failed ($LASTEXITCODE)"
    }
    return $output
}

function Assert-CleanTree([string]$When) {
    # Explicit flags, so no user git config (status.showUntrackedFiles,
    # submodule.*.ignore) can hide an untracked or changed file.
    $status = @(Get-GitOutput @('status', '--porcelain', '--untracked-files=all', '--ignore-submodules=none'))
    $status = @($status | Where-Object { $_ -ne '' })
    if ($status.Count -ne 0) {
        foreach ($line in $status) {
            Write-Output "  $line"
        }
        Exit-Failed "the git tree is not clean $When (commit or remove the changes above; untracked files count)"
    }
}

function Get-CMakeVersion {
    $cmakeLists = [System.IO.File]::ReadAllText((Join-Path $repo 'CMakeLists.txt'))
    $versionMatch = [regex]::Match($cmakeLists, 'set\(CTR_NATIVE_VERSION "([^"]+)"\)')
    if (-not $versionMatch.Success) {
        Exit-Failed 'CTR_NATIVE_VERSION not found in CMakeLists.txt'
    }
    return $versionMatch.Groups[1].Value
}

# Never delete through a junction or symlink: a reparse point anywhere on the
# path the script recreates could redirect Remove-Item outside the build tree.
function Assert-NoReparsePoint([string[]]$Paths) {
    foreach ($guardedPath in $Paths) {
        if (Test-Path -LiteralPath $guardedPath) {
            $guardedItem = Get-Item -LiteralPath $guardedPath -Force
            if (($guardedItem.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                Exit-Failed "refusing to write through a junction or symbolic link: $guardedPath"
            }
        }
    }
}

# The one staging path of both modes: recreates $Destination (already checked
# by the caller), copies $SourceExe as ctr_native.exe and the templates
# verbatim, writes MANIFEST.txt ($HeaderLines, the same-build note, and the
# size and SHA-256 of every other file), and runs the guard. A folder that
# fails the guard is deleted and the script exits 1.
function Write-PackageFolder([string]$SourceExe, [string]$Destination, [string[]]$HeaderLines) {
    if (Test-Path -LiteralPath $Destination) {
        Remove-Item -LiteralPath $Destination -Recurse -Force
    }
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null

    Copy-Item -LiteralPath $SourceExe -Destination (Join-Path $Destination 'ctr_native.exe')
    foreach ($template in @('cab1.cfg', 'cab2.cfg', 'README.txt')) {
        Copy-Item -LiteralPath (Join-Path $templates $template) -Destination (Join-Path $Destination $template)
    }

    $manifest = New-Object System.Collections.Generic.List[string]
    foreach ($headerLine in $HeaderLines) {
        $manifest.Add($headerLine)
    }
    $manifest.Add('')
    $manifest.Add('Both cabinets must show the same ctr_native.exe SHA-256 (Get-FileHash ctr_native.exe -Algorithm SHA256); the link handshake rejects different builds.')
    $manifest.Add('')
    $manifest.Add('files (name, size in bytes, SHA-256):')
    foreach ($file in @('ctr_native.exe', 'cab1.cfg', 'cab2.cfg', 'README.txt')) {
        $path = Join-Path $Destination $file
        $size = (Get-Item -LiteralPath $path).Length
        $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
        $manifest.Add("$file  $size  $hash")
    }
    [System.IO.File]::WriteAllText((Join-Path $Destination 'MANIFEST.txt'), (($manifest -join "`r`n") + "`r`n"), (New-Object System.Text.ASCIIEncoding))

    $violations = Get-GuardViolations $Destination
    Write-GuardResult $Destination $violations
    if ($violations.Count -ne 0) {
        Remove-Item -LiteralPath $Destination -Recurse -Force
        Exit-Failed "the package failed the retail-data guard and was deleted"
    }
}

function Write-PackageSummary([string]$Destination, [string]$VersionText) {
    $exeItem = Get-Item -LiteralPath (Join-Path $Destination 'ctr_native.exe')
    $exeHash = (Get-FileHash -LiteralPath $exeItem.FullName -Algorithm SHA256).Hash
    Write-Output ''
    Write-Output "package: $Destination"
    Write-Output "version: $VersionText"
    Write-Output "ctr_native.exe SHA-256: $exeHash"
    Write-Output "ctr_native.exe size: $($exeItem.Length) bytes"
    Write-Output 'files:'
    foreach ($item in @(Get-ChildItem -LiteralPath $Destination -Force | Sort-Object Name)) {
        Write-Output ("  {0}  {1}" -f $item.Name, $item.Length)
    }
}

# ---------------------------------------------------------------------------
# Stage mode: a test package from a given exe, for the package smoke gate
# (tools/package-arcade-smoke.ps1). No clean-tree, build, or --version check,
# so the MANIFEST says so and names the package ctr-arcade-staged.

if ($stageRequested) {
    if ([string]::IsNullOrWhiteSpace($StageExecutable) -or [string]::IsNullOrWhiteSpace($StageDirectory)) {
        Exit-Failed '-StageExecutable and -StageDirectory must be given together'
    }
    if (-not (Test-Path -LiteralPath $StageExecutable -PathType Leaf)) {
        Exit-Failed "stage executable not found: $StageExecutable"
    }
    $stageExe = (Resolve-Path -LiteralPath $StageExecutable).ProviderPath
    $stageDestination = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($StageDirectory)
    $stageDestination = [System.IO.Path]::GetFullPath($stageDestination).TrimEnd('\')
    $buildPrefix = $buildDir.TrimEnd('\') + '\'
    if (-not $stageDestination.StartsWith($buildPrefix, [System.StringComparison]::OrdinalIgnoreCase) -or
        ($stageDestination.Length -le $buildPrefix.Length)) {
        Exit-Failed "refusing stage destination outside ${buildPrefix}: $stageDestination"
    }
    if ($stageDestination.StartsWith('C:\arcade', [System.StringComparison]::OrdinalIgnoreCase)) {
        Exit-Failed "refusing stage destination under C:\arcade: $stageDestination"
    }
    if ($stageExe.StartsWith($stageDestination + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
        Exit-Failed "refusing to stage an exe from inside the stage destination (it is recreated): $stageExe"
    }
    # Every folder from build-msvc-x86 down to the destination.
    $guardedPaths = New-Object System.Collections.Generic.List[string]
    $walk = $stageDestination
    while ($walk.Length -gt $buildDir.TrimEnd('\').Length) {
        $guardedPaths.Add($walk)
        $walk = Split-Path -Parent $walk
    }
    $guardedPaths.Add($buildDir)
    Assert-NoReparsePoint $guardedPaths.ToArray()
    # Only a package folder is ever recreated: a mistyped destination (a build
    # output folder, say) is refused, not deleted.
    if (Test-Path -LiteralPath $stageDestination) {
        if (-not (Test-Path -LiteralPath $stageDestination -PathType Container)) {
            Exit-Failed "refusing stage destination that is not a folder: $stageDestination"
        }
        foreach ($entry in @(Get-ChildItem -LiteralPath $stageDestination -Force)) {
            if ($entry.PSIsContainer -or ($allowedFiles -cnotcontains $entry.Name)) {
                Exit-Failed "refusing to recreate ${stageDestination}: it holds $($entry.Name), which is not a package file"
            }
        }
    }

    $version = Get-CMakeVersion
    Write-PackageFolder $stageExe $stageDestination @(
        'CTR Native arcade package',
        'package: ctr-arcade-staged',
        "version: $version (from CMakeLists.txt; not checked against the exe)",
        'commit: not checked (staged test package)',
        'short commit: not checked (staged test package)',
        'configuration: not checked (staged test package)',
        'STAGED TEST PACKAGE: written by tools/package-arcade.ps1 -StageExecutable without the clean-tree, build, and --version checks. Never deploy it.')
    Write-PackageSummary $stageDestination 'not checked (staged test package)'
    exit 0
}

# ---------------------------------------------------------------------------
# Package build.

Assert-CleanTree 'before the build'

Push-Location $repo
try {
    & cmake --preset windows-msvc-x86 | Out-Host
    if ($LASTEXITCODE -ne 0) {
        Exit-Failed "cmake --preset windows-msvc-x86 failed ($LASTEXITCODE)"
    }
    & cmake --build build-msvc-x86 --config Release --target ctr_native | Out-Host
    if ($LASTEXITCODE -ne 0) {
        Exit-Failed "cmake --build build-msvc-x86 --config Release --target ctr_native failed ($LASTEXITCODE)"
    }
}
finally {
    Pop-Location
}

Assert-CleanTree 'after the build'

$fullHash = [string](Get-GitOutput @('rev-parse', 'HEAD'))
$shortHash = [string](Get-GitOutput @('rev-parse', '--short=12', 'HEAD'))
$fullHash = $fullHash.Trim()
$shortHash = $shortHash.Trim()
if ($shortHash -notmatch '^[0-9a-f]{12}$') {
    Exit-Failed "unexpected short commit hash '$shortHash'"
}

$version = Get-CMakeVersion

$exe = Join-Path $buildDir 'Release\ctr_native.exe'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    Exit-Failed "Release exe not found: $exe"
}
$versionOutput = @(& $exe --version)
if ($LASTEXITCODE -ne 0) {
    Exit-Failed "$exe --version failed ($LASTEXITCODE)"
}
$expectedVersion = "CTR Native $version ($shortHash)"
$actualVersion = ($versionOutput -join "`n").Trim()
if ($actualVersion -cne $expectedVersion) {
    Exit-Failed "$exe --version printed '$actualVersion', expected '$expectedVersion'"
}

$packageName = "ctr-arcade-$shortHash"
$destination = [System.IO.Path]::GetFullPath((Join-Path $packageRoot $packageName))
$rootPrefix = $packageRoot.TrimEnd('\') + '\'
if (-not $destination.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase) -or
    ($destination.TrimEnd('\').Length -le $rootPrefix.Length)) {
    Exit-Failed "refusing destination outside ${rootPrefix}: $destination"
}
if ($destination.StartsWith('C:\arcade', [System.StringComparison]::OrdinalIgnoreCase)) {
    Exit-Failed "refusing destination under C:\arcade: $destination"
}

Assert-NoReparsePoint @($buildDir, $packageRoot, $destination)

Write-PackageFolder $exe $destination @(
    'CTR Native arcade package',
    "package: $packageName",
    "version: $version",
    "commit: $fullHash",
    "short commit: $shortHash",
    'configuration: Release')
Write-PackageSummary $destination $actualVersion
exit 0
