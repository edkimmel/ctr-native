param(
    [Parameter(Mandatory = $true)][string]$AsmDirectory,
    [int]$BudgetBytes = 1536,
    [string]$Root = '_MainCanonicalRuntime_PrepareV3'
)

$ErrorActionPreference = 'Stop'
$leafCalls = @{
    '_memcpy' = 4
    '_memset' = 4
    '_malloc' = 4
    '_calloc' = 4
    '_free' = 4
    '__allmul' = 4
    '__aulldiv' = 4
    '__aullrem' = 4
    '__allshl' = 4
    '__aullshr' = 4
    '@__security_check_cookie@4' = 4
}
$functions = @{}

$asmFiles = @(Get-ChildItem -LiteralPath $AsmDirectory -Filter '*.cod' -File)
if ($asmFiles.Count -eq 0) { throw "No MSVC /FAcs listings found in $AsmDirectory" }

foreach ($asmFile in $asmFiles) {
    $current = $null
    $baseBytes = 4 # return address present on function entry
    $localBytes = 0
    $saved = @{}
    $outgoingBytes = 0
    $maxOwnBytes = 4
    $calls = @()
    $seenFirstCall = $false
    $dynamicReason = $null
    foreach ($line in Get-Content -LiteralPath $asmFile.FullName) {
        if ($null -eq $current) {
            if ($line -match '^([?@A-Za-z0-9_$]+)\s+PROC\b') {
                $current = $Matches[1]
                $baseBytes = 4
                $localBytes = 0
                $saved = @{}
                $outgoingBytes = 0
                $maxOwnBytes = 4
                $calls = @()
                $seenFirstCall = $false
                $dynamicReason = $null
            }
            continue
        }
        if ($line -match ('^' + [regex]::Escape($current) + '\s+ENDP\b')) {
            $record = [pscustomobject]@{ Name=$current; Base=$baseBytes+$localBytes; Peak=$maxOwnBytes; Calls=$calls; DynamicReason=$dynamicReason }
            if ($functions.ContainsKey($current)) {
                if ($record.Peak -gt $functions[$current].Peak) { $functions[$current] = $record }
            } else { $functions[$current] = $record }
            $current = $null
            continue
        }
        if ($line -match '\bsub\s+esp,\s*([A-Za-z][A-Za-z0-9]*)\b') {
            if ($null -eq $dynamicReason) { $dynamicReason = "dynamic stack allocation: $line" }
            continue
        }
        if ($line -match '\bsub\s+esp,\s*([0-9]+)\b') {
            if (!$seenFirstCall) { $localBytes += [int]$Matches[1] }
            else { $outgoingBytes += [int]$Matches[1] }
            $maxOwnBytes = [math]::Max($maxOwnBytes, $baseBytes + $localBytes + 4*$saved.Count + $outgoingBytes)
            continue
        }
        if ($line -match '\band\s+esp,') {
            if ($null -eq $dynamicReason) { $dynamicReason = "dynamic stack realignment: $line" }
            continue
        }
        if (!$seenFirstCall -and $line -match '\bpush\s+(ebp|ebx|esi|edi)\b') {
            $register = $Matches[1]
            if (!$saved.ContainsKey($register)) {
                $saved[$register] = $true
                $maxOwnBytes = [math]::Max($maxOwnBytes, $baseBytes + $localBytes + 4*$saved.Count)
                continue
            }
        }
        if ($line -match '^\$[A-Za-z0-9_?@$]+:\s*$') { $outgoingBytes = 0; continue }
        if ($line -match '\bpush\s+') {
            $outgoingBytes += 4
            $maxOwnBytes = [math]::Max($maxOwnBytes, $baseBytes + $localBytes + 4*$saved.Count + $outgoingBytes)
            continue
        }
        if ($line -match '\badd\s+esp,\s*([0-9]+)\b') {
            $outgoingBytes = [math]::Max(0, $outgoingBytes - [int]$Matches[1])
            continue
        }
        if ($line -match '\bcall\s+([^\s;]+)') {
            $seenFirstCall = $true
            $target = $Matches[1]
            if ($target -match '^(DWORD|WORD|BYTE)$' -or $target -match '^\[') {
                if ($null -eq $dynamicReason) { $dynamicReason = "indirect call: $line" }
                continue
            }
            $calls += [pscustomobject]@{ Target=$target; At=$baseBytes+$localBytes+4*$saved.Count+$outgoingBytes }
            continue
        }
    }
    if ($null -ne $current) { throw "Unterminated PROC $current in $($asmFile.Name)" }
}

if (!$functions.ContainsKey($Root)) { throw "Root function $Root was not found" }
$visiting = @{}
$memo = @{}
$paths = @{}
function Get-WorstStack([string]$name) {
    if ($memo.ContainsKey($name)) { return [int]$memo[$name] }
    if ($visiting.ContainsKey($name)) { throw "Recursive direct-call cycle reaches $name" }
    if ($leafCalls.ContainsKey($name)) { $paths[$name] = @($name); return [int]$leafCalls[$name] }
    if (!$functions.ContainsKey($name)) { throw "Unresolved direct call reaches $name" }
    if ($null -ne $functions[$name].DynamicReason) { throw "Unsafe stack construct reaches ${name}: $($functions[$name].DynamicReason)" }
    $visiting[$name] = $true
    $fn = $functions[$name]
    $worst = [int]$fn.Peak
    $worstPath = @($name)
    foreach ($call in $fn.Calls) {
        $child = Get-WorstStack $call.Target
        $candidate = [int]$call.At + $child
        if ($candidate -gt $worst) {
            $worst = $candidate
            $worstPath = @($name) + @($paths[$call.Target])
        }
    }
    $visiting.Remove($name)
    $memo[$name] = $worst
    $paths[$name] = $worstPath
    return $worst
}

$worstBytes = Get-WorstStack $Root
$pathText = ($paths[$Root] -join ' -> ')
Write-Host "runtime stack: $worstBytes bytes; budget: $BudgetBytes bytes"
Write-Host "worst direct-call chain: $pathText"
foreach ($name in $paths[$Root]) {
    if ($functions.ContainsKey($name)) {
        Write-Host "  $name base=$($functions[$name].Base) own-peak=$($functions[$name].Peak)"
    } else { Write-Host "  $name external-leaf=$($leafCalls[$name])" }
}
if ($worstBytes -gt $BudgetBytes) { throw "Runtime stack budget exceeded: $worstBytes > $BudgetBytes" }
