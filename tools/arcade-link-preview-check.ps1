[CmdletBinding()]
param(
    # Internal ctr_native build that accepts --arcade-link-preview.
    [Parameter(Mandatory = $true)]
    [string]$Executable,

    # ctr_native_arcade_link_capture_check.
    [Parameter(Mandatory = $true)]
    [string]$Checker,

    # Absolute directory for captures, per-run logs, and review PNGs.  Each
    # run uses it as its working directory; the game itself still writes the
    # gitignored `Crash Team Racing.log` in the repository root.
    [Parameter(Mandatory = $true)]
    [string]$OutputDirectory,

    # User-supplied disc image.  The check skips (exit 77) when it is absent.
    # Defaults to <repo>\assets\ctr-u.bin, resolved below: Windows PowerShell
    # 5.1 leaves $PSScriptRoot empty inside the param defaults of a
    # [CmdletBinding()] script run with -File.
    [string]$AssetsFile,

    [string[]]$Screens = @(
        'title', 'lobby', 'lobby-connecting', 'lobby-rejected', 'match-found', 'results',
        'results-timeout', 'results-desync', 'results-link-error', 'rematch', 'exit', 'exit-opponent-left',
        'select-character', 'select-track', 'select-laps', 'select-wait', 'select-result'),

    [int]$Frame = 1320,

    [int]$ExitFrame = 1330,

    [int]$TimeoutSeconds = 300,

    # Also capture the default path (no arcade-link option) and require the
    # checker to reject it as 'title' and as every selected screen: the
    # default path has no link panel.
    [switch]$IncludeDefault,

    # Write <name>-review.png next to each capture, alpha forced opaque.
    [switch]$Png
)

# Exit codes: 0 pass, 1 fail, 77 skipped (ctest SKIP_RETURN_CODE).
$skipExitCode = 77
$allScreens = @(
    'title', 'lobby', 'lobby-connecting', 'lobby-rejected', 'match-found', 'results',
    'results-timeout', 'results-desync', 'results-link-error', 'rematch', 'exit', 'exit-opponent-left',
    'select-character', 'select-track', 'select-laps', 'select-wait', 'select-result')
$noDisplayMarker = 'No displays available'
$notInternalMarker = '--arcade-link-preview is available in internal builds only.'

function Exit-Skipped([string]$Reason) {
    Write-Output "SKIPPED: $Reason"
    exit $skipExitCode
}

function Exit-Failed([string]$Reason) {
    Write-Output "FAIL: $Reason"
    exit 1
}

# Start-Process joins its argument array with spaces, so quote any element
# that needs it.  A quote pair in the middle of an argument (1320="C:\a b")
# is still one argv element to the C runtime.
function ConvertTo-ProcessArgument([string]$Value) {
    if ($Value -notmatch '[\s"]') {
        return $Value
    }
    $index = $Value.IndexOf('=')
    if (($index -gt 0) -and ($Value.Substring(0, $index) -match '^[0-9]+$')) {
        return $Value.Substring(0, $index + 1) + '"' + $Value.Substring($index + 1).TrimEnd('\') + '"'
    }
    return '"' + $Value.TrimEnd('\') + '"'
}

function Get-RunLog($Run) {
    $text = ''
    foreach ($path in @($Run.StdoutPath, $Run.StderrPath)) {
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            $text += [System.IO.File]::ReadAllText($path)
            $text += "`n"
        }
    }
    return $text
}

function Stop-StartedRuns($Runs) {
    foreach ($run in $Runs) {
        if (-not $run.Process.HasExited) {
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

function Write-ReviewPng([string]$BmpPath, [string]$PngPath, [string]$TempDirectory) {
    [byte[]]$bytes = [System.IO.File]::ReadAllBytes($BmpPath)
    if (($bytes.Length -lt 54) -or ($bytes[0] -ne 0x42) -or ($bytes[1] -ne 0x4D)) {
        throw "Not a BMP file: $BmpPath"
    }
    $pixelOffset = [BitConverter]::ToUInt32($bytes, 10)
    $width = [BitConverter]::ToInt32($bytes, 18)
    $height = [Math]::Abs([BitConverter]::ToInt32($bytes, 22))
    $bitsPerPixel = [BitConverter]::ToUInt16($bytes, 28)
    if ($bitsPerPixel -eq 32) {
        # The capture's alpha byte is the PS1 mask bit, mostly 0.  Force it
        # opaque so a viewer shows the RGB the checker measured.
        $end = [Math]::Min([long]$bytes.Length, [long]$pixelOffset + ([long]$width * [long]$height * 4))
        for ($i = [long]$pixelOffset + 3; $i -lt $end; $i += 4) {
            $bytes[$i] = 255
        }
    }
    $tempPath = Join-Path $TempDirectory ([System.IO.Path]::GetFileNameWithoutExtension($PngPath) + '.opaque.tmp.bmp')
    [System.IO.File]::WriteAllBytes($tempPath, $bytes)
    try {
        $image = New-Object System.Drawing.Bitmap $tempPath
        try {
            $image.Save($PngPath, [System.Drawing.Imaging.ImageFormat]::Png)
        }
        finally {
            $image.Dispose()
        }
    }
    finally {
        Remove-Item -LiteralPath $tempPath -Force -ErrorAction SilentlyContinue
    }
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
$resolvedExecutable = (Resolve-Path -LiteralPath $Executable -ErrorAction Stop).Path
$resolvedChecker = (Resolve-Path -LiteralPath $Checker -ErrorAction Stop).Path
$resolvedOutput = [System.IO.Path]::GetFullPath($OutputDirectory)
[System.IO.Directory]::CreateDirectory($resolvedOutput) | Out-Null

# -Screens a,b arrives as one string under powershell -File; accept both forms.
$selectedScreens = @()
foreach ($entry in $Screens) {
    foreach ($name in ($entry -split ',')) {
        $name = $name.Trim()
        if ($name -eq '') {
            continue
        }
        if ($allScreens -notcontains $name) {
            Exit-Failed "unknown screen '$name'. Supported: $($allScreens -join ', ')"
        }
        if ($selectedScreens -notcontains $name) {
            $selectedScreens += $name
        }
    }
}
if ($selectedScreens.Count -eq 0) {
    Exit-Failed 'no screens selected'
}
if (($Frame -lt 1) -or ($ExitFrame -lt $Frame)) {
    Exit-Failed "invalid frames: capture $Frame, exit $ExitFrame"
}

$runSpecs = @()
foreach ($screen in $selectedScreens) {
    $runSpecs += [pscustomobject]@{ Name = $screen; Screen = $screen; IsDefault = $false }
}
if ($IncludeDefault) {
    $runSpecs += [pscustomobject]@{ Name = 'default'; Screen = 'title'; IsDefault = $true }
}

Write-Output "arcade-link preview check: $($runSpecs.Count) run(s), capture frame $Frame, exit frame $ExitFrame"
Write-Output "executable: $resolvedExecutable"
Write-Output "output:     $resolvedOutput"

$runs = @()
foreach ($spec in $runSpecs) {
    $bmpPath = Join-Path $resolvedOutput ($spec.Name + '.bmp')
    $pngPath = Join-Path $resolvedOutput ($spec.Name + '-review.png')
    $stdoutPath = Join-Path $resolvedOutput ($spec.Name + '.stdout.log')
    $stderrPath = Join-Path $resolvedOutput ($spec.Name + '.stderr.log')
    # A stale capture from an earlier run must never pass for this one.
    foreach ($stale in @($bmpPath, $pngPath, $stdoutPath, $stderrPath)) {
        if (Test-Path -LiteralPath $stale) {
            Remove-Item -LiteralPath $stale -Force -ErrorAction Stop
        }
    }

    $arguments = @()
    if (-not $spec.IsDefault) {
        $arguments += @('--arcade-link-preview', $spec.Screen)
    }
    $arguments += @('--capture-frame', "$Frame=$bmpPath", '--exit-after-frame', "$ExitFrame")
    $argumentLine = ($arguments | ForEach-Object { ConvertTo-ProcessArgument $_ }) -join ' '

    $process = Start-Process -FilePath $resolvedExecutable -ArgumentList $argumentLine `
        -WorkingDirectory $resolvedOutput -NoNewWindow -PassThru `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    # Touching Handle keeps ExitCode readable after the process exits.
    $null = $process.Handle
    $runs += [pscustomobject]@{
        Name = $spec.Name
        Screen = $spec.Screen
        IsDefault = $spec.IsDefault
        Process = $process
        BmpPath = $bmpPath
        PngPath = $pngPath
        StdoutPath = $stdoutPath
        StderrPath = $stderrPath
        StartedAt = Get-Date
    }
}

# Wait for every run.  A missing display or a non-internal build is known as
# soon as one run exits, so stop the rest early instead of waiting them out.
$deadline = (Get-Date).AddSeconds($TimeoutSeconds)
$skipReason = $null
while ($true) {
    $pending = @($runs | Where-Object { -not $_.Process.HasExited })
    foreach ($run in @($runs | Where-Object { $_.Process.HasExited })) {
        $log = Get-RunLog $run
        if ($log.Contains($noDisplayMarker)) {
            $skipReason = "no display available to SDL (run '$($run.Name)', see $($run.StderrPath)); a desktop session is required"
        }
        elseif ((-not $run.IsDefault) -and $log.Contains($notInternalMarker)) {
            $skipReason = "--arcade-link-preview was rejected: the executable is not an internal (CTR_INTERNAL) build (run '$($run.Name)', see $($run.StderrPath))"
        }
        if ($null -ne $skipReason) {
            break
        }
    }
    if ($null -ne $skipReason) {
        Stop-StartedRuns $runs
        Exit-Skipped $skipReason
    }
    if ($pending.Count -eq 0) {
        break
    }
    if ((Get-Date) -gt $deadline) {
        Stop-StartedRuns $runs
        Exit-Failed "timed out after $TimeoutSeconds s waiting for: $(($pending | ForEach-Object { $_.Name }) -join ', ') (logs in $resolvedOutput)"
    }
    Start-Sleep -Milliseconds 500
}
foreach ($run in $runs) {
    $run.Process.WaitForExit()
}

$failures = @()
foreach ($run in $runs) {
    $exitCode = $run.Process.ExitCode
    $seconds = [math]::Round(($run.Process.ExitTime - $run.StartedAt).TotalSeconds, 1)
    $log = Get-RunLog $run
    if ($exitCode -ne 0) {
        $failures += "run '$($run.Name)' exited $exitCode; see $($run.StdoutPath) and $($run.StderrPath)"
    }
    elseif ((-not $run.IsDefault) -and (-not $log.Contains("arcade link: preview $($run.Screen)"))) {
        $failures += "run '$($run.Name)' did not log 'arcade link: preview $($run.Screen)'; see $($run.StdoutPath)"
    }
    elseif (-not (Test-Path -LiteralPath $run.BmpPath -PathType Leaf)) {
        $failures += "run '$($run.Name)' exited 0 but wrote no capture $($run.BmpPath); see $($run.StdoutPath)"
    }
    Write-Output ("run {0,-20} exit {1} in {2} s" -f $run.Name, $exitCode, $seconds)
}
if ($failures.Count -ne 0) {
    foreach ($failure in $failures) {
        Write-Output "FAIL: $failure"
    }
    Write-Output "arcade-link preview check: FAIL ($($failures.Count) of $($runs.Count) run(s) failed to capture)"
    exit 1
}

$checked = 0
foreach ($run in $runs) {
    Write-Output ''
    $checkerOutput = & $resolvedChecker $run.BmpPath $run.Screen 2>&1 | ForEach-Object { "$_" }
    $checkerExit = $LASTEXITCODE
    foreach ($line in $checkerOutput) {
        Write-Output $line
    }
    if ($run.IsDefault) {
        if ($checkerExit -eq 1) {
            Write-Output "default path: checker rejected default.bmp as '$($run.Screen)' (exit 1) as required; the default path has no arcade-link panel"
        }
        else {
            $failures += "default path: checker exit $checkerExit on $($run.BmpPath) as '$($run.Screen)'; required exit 1 (no arcade-link panel)"
        }
        # The default path must fail as every other selected screen too.
        foreach ($other in $selectedScreens) {
            if ($other -eq $run.Screen) {
                continue
            }
            $otherOutput = & $resolvedChecker $run.BmpPath $other 2>&1 | ForEach-Object { "$_" }
            $otherExit = $LASTEXITCODE
            if ($otherExit -eq 1) {
                Write-Output "default path: checker rejected default.bmp as '$other' (exit 1): $(@($otherOutput)[-1])"
            }
            else {
                $failures += "default path: checker exit $otherExit on $($run.BmpPath) as '$other'; required exit 1 (no arcade-link panel)"
            }
        }
    }
    elseif ($checkerExit -ne 0) {
        $failures += "screen '$($run.Screen)': checker exit $checkerExit on $($run.BmpPath)"
    }
    $checked++
}

if ($Png) {
    Add-Type -AssemblyName System.Drawing
    Write-Output ''
    foreach ($run in $runs) {
        try {
            Write-ReviewPng $run.BmpPath $run.PngPath $resolvedOutput
            Write-Output "review png: $($run.PngPath)"
        }
        catch {
            $failures += "review png for '$($run.Name)' failed: $($_.Exception.Message)"
        }
    }
}

Write-Output ''
if ($failures.Count -ne 0) {
    foreach ($failure in $failures) {
        Write-Output "FAIL: $failure"
    }
    Write-Output "arcade-link preview check: FAIL ($($failures.Count) failure(s) across $checked checked capture(s))"
    exit 1
}
$defaultNote = ''
if ($IncludeDefault) {
    $defaultNote = ' + default path rejected as title and every selected screen'
}
Write-Output "arcade-link preview check: PASS ($($selectedScreens.Count) screen(s)$defaultNote)"
exit 0
