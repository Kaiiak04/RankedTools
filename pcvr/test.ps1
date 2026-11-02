param(
    [ValidateSet('1.40.8','1.44.1')][string[]]$GameVersions=@('1.40.8','1.44.1'),
    [string]$NativeBuild='',
    [switch]$LiveHttps,
    [switch]$SkipNativeTests
)
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if(-not $NativeBuild){$NativeBuild=Join-Path $root 'build/pcvr-native'}
$NativeBuild=[IO.Path]::GetFullPath($NativeBuild)
$dll=Join-Path $NativeBuild 'Release/RankedTools.Core.dll'
if(-not(Test-Path -LiteralPath $dll)){$dll=Join-Path $NativeBuild 'RankedTools.Core.dll'}
if(-not(Test-Path -LiteralPath $dll)){throw 'Build the Windows native engine before running checks.'}
function Clear-ManagedSandbox([string]$name) {
    $parent=Join-Path $root 'tests/pcvr-sandbox'
    $path=Join-Path $parent $name
    if(Test-Path -LiteralPath $path) {
        $resolved=(Resolve-Path -LiteralPath $path).Path
        if(-not $resolved.StartsWith($parent+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Unexpected test sandbox path.'}
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}
Push-Location $root
try {
    $ctest=(Get-Command ctest -ErrorAction SilentlyContinue).Source
    if(-not $ctest){$ctest=Join-Path $root '.tools/cmake/bin/ctest.exe'}
    if($SkipNativeTests) {
        $nativeReport=Get-Content -LiteralPath (Join-Path $NativeBuild 'native-validation.json') -Raw | ConvertFrom-Json
        if($nativeReport.offlineChecks -ne 'passed' -or $nativeReport.nativeSha256 -ne (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash){throw 'The downloaded native engine has no matching successful regression report.'}
    } else {
        & $ctest --test-dir $NativeBuild -C Release --output-on-failure
        if($LASTEXITCODE -ne 0){throw 'Native regression checks failed.'}
    }
    $project=Join-Path $PSScriptRoot 'RankedTools.Pcvr.Tests/RankedTools.Pcvr.Tests.csproj'
    & dotnet restore $project --locked-mode --packages (Join-Path $root '.cache/pcvr/nuget')
    if($LASTEXITCODE -ne 0){throw 'Test dependency restore failed.'}
    & dotnet build $project -c Release --no-restore
    if($LASTEXITCODE -ne 0){throw 'Managed check compilation failed.'}
    $arguments=@()
    foreach($game in $GameVersions) {
        $plugin=Join-Path $PSScriptRoot "RankedTools.Pcvr/bin/Release/$game/RankedTools.dll"
        if(-not(Test-Path -LiteralPath $plugin)){throw "Missing plugin build: $game"}
        $arguments+=@('--plugin',$plugin)
    }
    $arguments+=@('--native',$dll)
    if($LiveHttps){$arguments+='--live'}
    $runner=Join-Path $PSScriptRoot 'RankedTools.Pcvr.Tests/bin/Release/net10.0/RankedTools.Pcvr.Tests.dll'
    Clear-ManagedSandbox 'managed-Å-测试'
    & dotnet $runner @arguments
    if($LASTEXITCODE -ne 0){throw 'Managed/binding/native interop checks failed.'}
    Clear-ManagedSandbox 'managed-Å-测试'
    if($LiveHttps) {
        Clear-ManagedSandbox 'managed-Å-测试-bad-ca'
        & dotnet $runner --native $dll --bad-ca
        if($LASTEXITCODE -ne 0){throw 'Invalid-CA rejection check failed.'}
        Clear-ManagedSandbox 'managed-Å-测试-bad-ca'
    }
    foreach($game in $GameVersions) {
        $report=@{gameVersion=$game; nativeSha256=(Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant(); pluginSha256=(Get-FileHash -LiteralPath (Join-Path $PSScriptRoot "RankedTools.Pcvr/bin/Release/$game/RankedTools.dll") -Algorithm SHA256).Hash.ToLowerInvariant(); offlineChecks='passed'; httpsSmoke= $(if($LiveHttps){'passed'}else{'not run'}); inGameValidation='pending'}
        $report | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $NativeBuild "validation-$game.json") -Encoding utf8
    }
} finally { Pop-Location }
