$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)))
$javac = (Get-Command javac -ErrorAction Stop).Source
$java = (Get-Command java -ErrorAction Stop).Source
$build = Join-Path $root 'build/provider-hooker-dispatcher-host-test'
$classes = Join-Path $build 'classes'
New-Item -ItemType Directory -Force -Path $classes | Out-Null
& $javac --release 11 -d $classes `
    (Join-Path $root 'provider-adapter/hooker/src/dev/pathguard/providerhook/ProviderHooker.java') `
    (Join-Path $root 'tests/unit/ProviderHookerDispatcherTest.java')
if ($LASTEXITCODE -ne 0) { throw 'ProviderHooker dispatcher javac failed' }
& $java -cp $classes ProviderHookerDispatcherTest
if ($LASTEXITCODE -ne 0) { throw 'ProviderHooker dispatcher host test failed' }
Write-Output 'ProviderHooker dispatcher host test passed'
