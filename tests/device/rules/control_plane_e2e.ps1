param(
    [Parameter(Mandatory = $true)] [string]$Daemon,
    [Parameter(Mandatory = $true)] [string]$Probe
)

$ErrorActionPreference = 'Stop'
$adb = (Get-Command adb -ErrorAction Stop).Source
$devices = @(& $adb devices | Select-Object -Skip 1 | Where-Object {
    $_ -match "\sdevice\s*$"
})
if ($devices.Count -ne 1) {
    throw "RF8 e2e requires exactly one device, got $($devices.Count)"
}
foreach ($file in @($Daemon, $Probe)) {
    if (-not (Test-Path -LiteralPath $file)) { throw "binary not found: $file" }
}
$remote = '/data/local/tmp/pathguard-rf8-e2e'
$local = Join-Path $env:TEMP ("pathguard-rf8-e2e-" + [guid]::NewGuid())
New-Item -ItemType Directory -Path $local | Out-Null
$rules = Join-Path $local 'rules.toml'
try {
    & $adb shell rm -rf $remote | Out-Null
    & $adb shell mkdir -p "$remote/config" "$remote/run"
    & $adb push $Daemon "$remote/pathguardd" | Out-Null
    & $adb push $Probe "$remote/zygisk-policy-probe" | Out-Null
    & $adb shell chmod 700 "$remote/pathguardd" "$remote/zygisk-policy-probe"
    [IO.File]::WriteAllText($rules,
        "format = 1`n[apps.`"com.example.app`"]`n" +
        "deny=[`"Pictures/Nagram`",`"DCIM/Screenshots`"]`n" +
        "redirect=[`"Source`" -> `"Target`"]`n",
        [Text.UTF8Encoding]::new($false))
    & $adb push $rules "$remote/config/rules.toml" | Out-Null
    & $adb shell chmod 600 "$remote/config/rules.toml"
    & $adb shell "$remote/pathguardd --module-dir $remote --compile" | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'daemon compile failed' }
    $before = (& $adb shell sha256sum "$remote/run/policy.bin") -join ''
    $plan = (& $adb shell "$remote/zygisk-policy-probe $remote/run/policy.bin com.example.app Source Target Pictures/Nagram DCIM/Screenshots") -join "`n"
    if ($LASTEXITCODE -ne 0 -or $plan -notmatch 'redirect=Source->Target denies=2') {
        throw "data-plane plan failed: $plan"
    }

    [IO.File]::WriteAllText($rules,
        "format = 1`n[apps.`"com.example.app`"]`nredirect=[`"Source`" ->]`n",
        [Text.UTF8Encoding]::new($false))
    & $adb push $rules "$remote/config/rules.toml" | Out-Null
    & $adb shell chmod 600 "$remote/config/rules.toml"
    & $adb shell "$remote/pathguardd --module-dir $remote --compile" 2>&1 | Out-Null
    if ($LASTEXITCODE -eq 0) { throw 'invalid candidate was accepted' }
    $after = (& $adb shell sha256sum "$remote/run/policy.bin") -join ''
    if ($before -ne $after) { throw 'failed candidate replaced active policy' }
    & $adb shell "$remote/zygisk-policy-probe $remote/run/policy.bin com.example.app Source Target Pictures/Nagram DCIM/Screenshots" | Out-Null
    if ($LASTEXITCODE -ne 0) { throw 'old generation is unreadable after failure' }
    Write-Host "RF8 Android vertical path passed: $plan"
} finally {
    & $adb shell rm -rf $remote | Out-Null
    Remove-Item -LiteralPath $local -Recurse -Force -ErrorAction SilentlyContinue
}
