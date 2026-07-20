param(
    [Parameter(Mandatory = $true)]
    [string]$SourceZip,
    [string]$ModuleId = 'pathguard_next_transaction_test'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path))
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$stage = Join-Path $root "build/device-transaction-test-$stamp"
$output = Join-Path $root "dist/pathguard-next-transaction-test-$stamp.zip"

New-Item -ItemType Directory -Path $stage | Out-Null
Expand-Archive -LiteralPath $SourceZip -DestinationPath $stage

$propertyFile = Join-Path $stage 'module.prop'
$propertyText = [IO.File]::ReadAllText($propertyFile)
$propertyText = $propertyText -replace '(?m)^id=.*$', "id=$ModuleId"
$propertyText = $propertyText -replace '(?m)^name=.*$', 'name=PathGuard Next Transaction Test'
$propertyText = $propertyText -replace '(?m)^version=.*$', 'version=0.1.3-dev-transaction-test'
$propertyText = $propertyText -replace '(?m)^versionCode=.*$', 'versionCode=900004'
[IO.File]::WriteAllText(
    $propertyFile, $propertyText, [Text.UTF8Encoding]::new($false))

Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $output `
    -CompressionLevel Optimal

Write-Output $output
