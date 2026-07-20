param(
    [Parameter(Mandatory = $true)]
    [string]$Daemon
)

$ErrorActionPreference = 'Stop'
$root = Join-Path $env:TEMP ("pathguard-hot-reload-" + [guid]::NewGuid())
$configDir = Join-Path $root 'config'
$runDir = Join-Path $root 'run'
New-Item -ItemType Directory -Path $configDir, $runDir | Out-Null
$config = Join-Path $configDir 'rules.ini'
$policy = Join-Path $runDir 'policy.bin'
$stdout = Join-Path $root 'stdout.log'
$stderr = Join-Path $root 'stderr.log'

$initial = @"
schema = 2
failure = open
[org.localsend.localsend_app]
users = 0
processes = *
deny Pictures/Nagram
"@
$comment_only = $initial + "`n# comment-only-change`n"
$updated = $comment_only + "deny DCIM`n"
Set-Content -LiteralPath $config -Value $initial -NoNewline

$process = Start-Process -FilePath $Daemon -ArgumentList '--module-dir', $root `
    -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru -WindowStyle Hidden
try {
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    while (-not (Test-Path -LiteralPath $policy) -and [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 100
    }
    if (-not (Test-Path -LiteralPath $policy)) { throw 'initial policy was not created' }
    $initialHash = (Get-FileHash -LiteralPath $policy -Algorithm SHA256).Hash
    $initialWriteTime = (Get-Item -LiteralPath $policy).LastWriteTimeUtc.Ticks

    Set-Content -LiteralPath $config -Value $comment_only -NoNewline
    Start-Sleep -Seconds 2
    if ((Get-FileHash -LiteralPath $policy -Algorithm SHA256).Hash -ne $initialHash) {
        throw 'comment-only change altered policy bytes'
    }
    if ((Get-Item -LiteralPath $policy).LastWriteTimeUtc.Ticks -ne $initialWriteTime) {
        throw 'comment-only change republished policy'
    }

    Set-Content -LiteralPath $config -Value $updated -NoNewline
    $deadline = [DateTime]::UtcNow.AddSeconds(5)
    do {
        Start-Sleep -Milliseconds 100
        $updatedHash = (Get-FileHash -LiteralPath $policy -Algorithm SHA256).Hash
    } while ($updatedHash -eq $initialHash -and [DateTime]::UtcNow -lt $deadline)

    if ($updatedHash -eq $initialHash) { throw 'policy was not reloaded' }
    Start-Sleep -Milliseconds 200
    $log = Get-Content -Raw -LiteralPath $stdout
    if ($log -notmatch 'policy unchanged') { throw 'unchanged policy was not logged' }
    if ($log -notmatch 'policy reloaded') { throw 'reload was not logged' }
} finally {
    if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force }
    Write-Output "Hot reload test data: $root"
}
