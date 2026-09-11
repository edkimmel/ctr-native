[CmdletBinding()]
param(
    [string]$BuildDirectory = "build-msvc-x86",
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [string]$CMakePath,
    [string]$PythonPath,
    [string]$AssetPath
)

$ErrorActionPreference = "Stop"
$sourceRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

if (-not [IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory = Join-Path $sourceRoot $BuildDirectory
}
$BuildDirectory = [IO.Path]::GetFullPath($BuildDirectory)

if (-not $CMakePath) {
    $cmakeCommand = Get-Command cmake -ErrorAction SilentlyContinue
    if ($cmakeCommand) {
        $CMakePath = $cmakeCommand.Source
    } else {
        $wingetRoot = Join-Path $env:LOCALAPPDATA "Microsoft\WinGet\Packages"
        $CMakePath = Get-ChildItem -Path $wingetRoot -Recurse -Filter cmake.exe -File -ErrorAction SilentlyContinue |
            Where-Object FullName -Match "Kitware\.CMake" |
            Sort-Object FullName -Descending |
            Select-Object -First 1 -ExpandProperty FullName
    }
}
if (-not $CMakePath -or -not (Test-Path -LiteralPath $CMakePath -PathType Leaf)) {
    throw "CMake 3.20 or newer is required. Pass -CMakePath if it is not on PATH."
}

$ctestPath = Join-Path (Split-Path -Parent $CMakePath) "ctest.exe"
if (-not (Test-Path -LiteralPath $ctestPath -PathType Leaf)) {
    throw "ctest.exe was not found beside $CMakePath"
}

if ($PythonPath) {
    $PythonPath = (Resolve-Path -LiteralPath $PythonPath).Path
}
if ($AssetPath) {
    $AssetPath = (Resolve-Path -LiteralPath $AssetPath).Path
}

New-Item -ItemType Directory -Force -Path $BuildDirectory | Out-Null

function Invoke-LoggedNative {
    param(
        [string]$Executable,
        [string[]]$Arguments,
        [string]$LogName
    )

    & $Executable @Arguments 2>&1 | Tee-Object -FilePath (Join-Path $BuildDirectory $LogName)
    if ($LASTEXITCODE -ne 0) {
        throw "$Executable failed with exit code $LASTEXITCODE. See $LogName."
    }
}

$configureArguments = @("--preset", "windows-msvc-x86", "-B", $BuildDirectory)
if ($PythonPath) {
    $configureArguments += "-DPython3_EXECUTABLE=$PythonPath"
}

Push-Location $sourceRoot
try {
    Invoke-LoggedNative $CMakePath $configureArguments "m1-configure.log"
    Invoke-LoggedNative $CMakePath @("--build", $BuildDirectory, "--config", $Configuration, "--parallel") "m1-build.log"
    Invoke-LoggedNative $ctestPath @("--test-dir", $BuildDirectory, "-C", $Configuration, "--output-on-failure") "m1-ctest.log"

    $executablePath = Join-Path (Join-Path $BuildDirectory $Configuration) "ctr_native.exe"
    if (-not (Test-Path -LiteralPath $executablePath -PathType Leaf)) {
        throw "Expected executable was not produced: $executablePath"
    }

    $peReportPath = Join-Path $BuildDirectory "m1-pe.txt"
    $vswherePath = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $dumpbinPath = $null
    if (Test-Path -LiteralPath $vswherePath -PathType Leaf) {
        $vsRoot = & $vswherePath -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($vsRoot) {
            $dumpbinPath = Get-ChildItem -Path (Join-Path $vsRoot "VC\Tools\MSVC") -Recurse -Filter dumpbin.exe -File |
                Where-Object FullName -Match "Hostx64\\x86\\dumpbin\.exe$" |
                Sort-Object FullName -Descending |
                Select-Object -First 1 -ExpandProperty FullName
        }
    }

    $peArchitecture = $null
    $imports = @()
    if ($dumpbinPath) {
        $peOutput = & $dumpbinPath /headers /dependents $executablePath 2>&1
        if ($LASTEXITCODE -ne 0) {
            throw "dumpbin failed with exit code $LASTEXITCODE"
        }
        $peOutput | Set-Content -Encoding UTF8 -LiteralPath $peReportPath
        $machineLine = $peOutput | Select-String "machine \(" | Select-Object -First 1
        if ($machineLine) {
            $peArchitecture = $machineLine.Line.Trim()
        }
        $imports = @($peOutput | ForEach-Object {
            if ($_ -match "^\s+([A-Za-z0-9_.-]+\.dll)\s*$") { $Matches[1] }
        } | Sort-Object -Unique)
    }

    $configureLog = Get-Content -LiteralPath (Join-Path $BuildDirectory "m1-configure.log")
    $compilerMatch = $configureLog | Select-String "The C compiler identification is (.+)$" | Select-Object -First 1
    $toolsetMatch = $configureLog | Select-String "Compiler: .*/MSVC/([^/]+)/" | Select-Object -First 1
    $sdkMatch = $configureLog | Select-String "Selecting Windows SDK version ([^ ]+)" | Select-Object -First 1
    $compiler = if ($compilerMatch) { $compilerMatch.Matches[0].Groups[1].Value } else { $null }
    $toolset = if ($toolsetMatch) { $toolsetMatch.Matches[0].Groups[1].Value } else { $null }
    $windowsSdk = if ($sdkMatch) { $sdkMatch.Matches[0].Groups[1].Value } else { $null }

    $asset = $null
    if ($AssetPath) {
        $assetInfo = Get-Item -LiteralPath $AssetPath
        $asset = [ordered]@{
            file = $assetInfo.Name
            size = $assetInfo.Length
            sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $AssetPath).Hash.ToLowerInvariant()
            divisibleBy2352 = (($assetInfo.Length % 2352) -eq 0)
            divisibleBy2048 = (($assetInfo.Length % 2048) -eq 0)
        }
    }

    $manifest = [ordered]@{
        schema = 1
        generatedUtc = [DateTime]::UtcNow.ToString("o")
        sourceCommit = (& git rev-parse HEAD).Trim()
        sourceStatus = @(& git status --porcelain --untracked-files=no)
        buildId = ((& $executablePath --version) -join " ").Trim()
        preset = "windows-msvc-x86"
        configuration = $Configuration
        cmake = ((& $CMakePath --version | Select-Object -First 1) -replace "^cmake version ", "")
        python = if ($PythonPath) { ((& $PythonPath --version) -join " ").Trim() } else { $null }
        compiler = $compiler
        toolset = $toolset
        windowsSdk = $windowsSdk
        executable = [ordered]@{
            file = "ctr_native.exe"
            size = (Get-Item -LiteralPath $executablePath).Length
            sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $executablePath).Hash.ToLowerInvariant()
            peArchitecture = $peArchitecture
            imports = $imports
            sdl = if ($imports -contains "SDL3.dll") { "dynamic" } else { "static (no SDL3.dll import)" }
        }
        tests = [ordered]@{ presetEquivalent = "windows-msvc-x86-$($Configuration.ToLowerInvariant())"; passed = $true }
        asset = $asset
        assetLookup = "assets/ctr-u.bin relative to the executable base, its parent, or grandparent"
        writablePaths = @("Crash Team Racing.log", "memcards/slot0", "memcards/slot1", "debug/states/quick.ctrstates", "debug/reports/<date>/ctr-<time>[-NN]/")
    }
    $manifest | ConvertTo-Json -Depth 6 | Set-Content -Encoding UTF8 -LiteralPath (Join-Path $BuildDirectory "m1-manifest.json")
} finally {
    Pop-Location
}

Write-Host "Baseline build, tests, PE report, and manifest completed in $BuildDirectory"
