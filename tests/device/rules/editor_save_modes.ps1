param(
    [Parameter(Mandatory = $true)]
    [string]$Daemon
)

$ErrorActionPreference = 'Stop'
$adb = (Get-Command adb -ErrorAction Stop).Source
$devices = @(& $adb devices | Select-Object -Skip 1 | Where-Object {
    $_ -match "\sdevice\s*$"
})
if ($devices.Count -ne 1) {
    throw "editor save test requires exactly one device, got $($devices.Count)"
}
if (-not (Test-Path -LiteralPath $Daemon)) {
    throw "daemon not found: $Daemon"
}

$remote = '/data/local/tmp/pathguard-rf7-editor-save'
$remoteDaemon = "$remote/pathguardd"
$remoteRules = "$remote/config/rules.toml"
$local = Join-Path $env:TEMP ("pathguard-rf7-editor-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $local | Out-Null

function Write-Rules([string]$Path, [string]$Target, [bool]$Bom,
                     [bool]$Crlf) {
    $text = "format = 1`n[apps.`"com.example.app`"]`nredirect = [`"A`" -> `"$Target`"]`n"
    if ($Crlf) { $text = $text.Replace("`n", "`r`n") }
    $encoding = [Text.UTF8Encoding]::new($Bom)
    [IO.File]::WriteAllText($Path, $text, $encoding)
}

function Invoke-Compile([bool]$ExpectSuccess) {
    $output = (& $adb shell "$remoteDaemon --module-dir $remote --compile" 2>&1) -join "`n"
    $ok = $LASTEXITCODE -eq 0
    if ($ok -ne $ExpectSuccess) {
        throw "unexpected daemon result (expected success=$ExpectSuccess):`n$output"
    }
    return $output
}

& $adb shell rm -rf $remote | Out-Null
try {
    & $adb shell mkdir -p "$remote/config" "$remote/run"
    & $adb push $Daemon $remoteDaemon | Out-Null
    & $adb shell chmod 700 $remoteDaemon

    $initial = Join-Path $local 'initial.toml'
    Write-Rules $initial 'Initial' $false $false
    & $adb push $initial $remoteRules | Out-Null
    & $adb shell chmod 600 $remoteRules
    Invoke-Compile $true | Out-Null

    $inPlace = Join-Path $local 'in-place.toml'
    Write-Rules $inPlace 'InPlace' $false $false
    & $adb push $inPlace $remoteRules | Out-Null
    & $adb shell chmod 600 $remoteRules
    Invoke-Compile $true | Out-Null

    $renamed = Join-Path $local 'renamed.toml'
    Write-Rules $renamed 'Renamed' $false $false
    & $adb push $renamed "$remote/config/rules.toml.new" | Out-Null
    & $adb shell chmod 600 "$remote/config/rules.toml.new"
    & $adb shell mv "$remote/config/rules.toml.new" $remoteRules
    Invoke-Compile $true | Out-Null

    $bomCrlf = Join-Path $local 'bom-crlf.toml'
    Write-Rules $bomCrlf 'BomCrlf' $true $true
    & $adb push $bomCrlf $remoteRules | Out-Null
    & $adb shell chmod 600 $remoteRules
    Invoke-Compile $true | Out-Null

    & $adb shell chmod 666 $remoteRules
    $unsafe = Invoke-Compile $false
    if ($unsafe -notmatch 'PG-SOURCE-UNSAFE') {
        throw "unsafe mode diagnostic is not actionable:`n$unsafe"
    }
    & $adb shell chmod 600 $remoteRules

    $first = Join-Path $local 'first.toml'
    $last = Join-Path $local 'last.toml'
    Write-Rules $first 'FirstSave' $false $false
    Write-Rules $last 'LastSave' $false $false
    & $adb push $first $remoteRules | Out-Null
    & $adb push $last $remoteRules | Out-Null
    & $adb shell chmod 600 $remoteRules
    Invoke-Compile $true | Out-Null
    $status = (& $adb shell cat "$remote/run/rules-status.txt") -join "`n"
    if ($status -notmatch 'status: active') {
        throw "final save did not converge:`n$status"
    }
    Write-Host 'Android editor save modes: in-place, rename, BOM/CRLF, mode, consecutive saves passed'
} finally {
    & $adb shell rm -rf $remote | Out-Null
    Remove-Item -LiteralPath $local -Recurse -Force -ErrorAction SilentlyContinue
}
