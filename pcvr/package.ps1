param(
    [ValidateSet('1.40.8','1.44.1')][string]$GameVersion='1.40.8',
    [string]$NativeBuild='',
    [string]$LlvmMingw=''
)
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if(-not $NativeBuild){$NativeBuild=Join-Path $root 'build/pcvr-native'}
$NativeBuild=[IO.Path]::GetFullPath($NativeBuild)
$native=Join-Path $NativeBuild 'Release/RankedTools.Core.dll'
if(-not(Test-Path -LiteralPath $native)){$native=Join-Path $NativeBuild 'RankedTools.Core.dll'}
$plugin=Join-Path $PSScriptRoot "RankedTools.Pcvr/bin/Release/$GameVersion/RankedTools.dll"
$reportPath=Join-Path $NativeBuild "validation-$GameVersion.json"
if(-not(Test-Path -LiteralPath $reportPath)){throw 'Run pcvr/test.ps1 -LiveHttps before packaging.'}
$report=Get-Content -LiteralPath $reportPath -Raw | ConvertFrom-Json
if($report.offlineChecks -ne 'passed' -or $report.httpsSmoke -ne 'passed' -or $report.nativeSha256 -ne (Get-FileHash -LiteralPath $native -Algorithm SHA256).Hash -or $report.pluginSha256 -ne (Get-FileHash -LiteralPath $plugin -Algorithm SHA256).Hash){throw 'Validation is missing or does not match the binaries. Run pcvr/test.ps1 -LiveHttps again.'}
$stage=Join-Path $root ('build/pcvr-package-'+$GameVersion+'-'+[Guid]::NewGuid().ToString('N'))
foreach($folder in @('Plugins','Libs/Native','UserData/RankedTools/assets','licenses')){New-Item -ItemType Directory -Path (Join-Path $stage $folder) -Force | Out-Null}
Copy-Item -LiteralPath $plugin -Destination (Join-Path $stage 'Plugins/RankedTools.dll')
Copy-Item -LiteralPath $native -Destination (Join-Path $stage 'Libs/Native/RankedTools.Core.dll')
Copy-Item -LiteralPath (Join-Path $root 'cacert.pem') -Destination (Join-Path $stage 'UserData/RankedTools/cacert.pem')
foreach($cover in @('sync','beatleader_notplayed','beatleader_toimprove','beatleader_ppgain','beatleader_clan_toconquer','scoresaber_notplayed','scoresaber_toimprove','scoresaber_ppgain')){Copy-Item -LiteralPath (Join-Path $root "$cover.png") -Destination (Join-Path $stage "UserData/RankedTools/assets/$cover.png")}
Copy-Item -LiteralPath (Join-Path $root 'assets/ARTWORK.md') -Destination (Join-Path $stage 'UserData/RankedTools/assets/ARTWORK.md')
foreach($file in @('LICENSE','THIRD_PARTY_NOTICES.md')){Copy-Item -LiteralPath (Join-Path $root $file) -Destination $stage}
foreach($file in @('README.md','TESTING.md','dependencies.lock.json','optional-dependencies.lock.json','targets.json')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot $file) -Destination $stage}
foreach($file in @('curl.txt','Mbed-TLS.txt','Mbed-TLS-third-party.txt','RapidJSON.txt')){Copy-Item -LiteralPath (Join-Path $root "licenses/$file") -Destination (Join-Path $stage 'licenses')}
$legato=Join-Path $root '.cache/pcvr/nuget/legatobs/0.1.1/LICENSE'
Copy-Item -LiteralPath $legato -Destination (Join-Path $stage 'licenses/Legato-GPL-3.0.txt')
$buildInfo=Get-Content -LiteralPath (Join-Path $NativeBuild 'native-build.json') -Raw | ConvertFrom-Json
if($buildInfo.compiler -eq 'Clang' -and -not $LlvmMingw){throw 'Supply -LlvmMingw so the portable compiler runtime notices are packaged.'}
if($buildInfo.compiler -eq 'Clang' -and $LlvmMingw) {
    Copy-Item -LiteralPath (Join-Path $LlvmMingw 'LICENSE.TXT') -Destination (Join-Path $stage 'licenses/LLVM.txt')
}
$commit=& git -C $root rev-parse HEAD
$buildInfo | Add-Member -NotePropertyName sourceCommit -NotePropertyValue $commit
$buildInfo | Add-Member -NotePropertyName gameVersion -NotePropertyValue $GameVersion
$buildInfo | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $stage 'build-info.json') -Encoding utf8
Copy-Item -LiteralPath $reportPath -Destination (Join-Path $stage 'validation.json')
$dist=Join-Path $PSScriptRoot 'dist'; New-Item -ItemType Directory -Path $dist -Force | Out-Null
$zip=Join-Path $dist "RankedTools-PCVR-0.1.26-preview.4-bs$GameVersion.zip"
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zip -Force
$archive=[IO.Compression.ZipFile]::OpenRead($zip)
try {
    foreach($entry in $archive.Entries) {
        $name=$entry.FullName.Replace('\','/')
        if($name.EndsWith('.dll') -and $name -notin @('Plugins/RankedTools.dll','Libs/Native/RankedTools.Core.dll')){throw "Unexpected packaged assembly: $name"}
        if($name -match '(^|/)(Beat Saber_Data|attempts|clan-search)(/|$)' -or $name -eq 'UserData/RankedTools.json'){throw "Unexpected packaged user/game data: $name"}
        if($name -in @('Plugins/RankedTools.dll','Libs/Native/RankedTools.Core.dll')) {
            $stream=$entry.Open()
            try { $hash=(Get-FileHash -InputStream $stream -Algorithm SHA256).Hash } finally { $stream.Dispose() }
            $expected=if($name -eq 'Plugins/RankedTools.dll'){$report.pluginSha256}else{$report.nativeSha256}
            if($hash -ne $expected){throw "Packaged binary differs from tested binary: $name"}
        }
    }
} finally { $archive.Dispose() }
Get-ChildItem -LiteralPath $dist -Filter 'RankedTools-PCVR-*.zip' | Sort-Object Name | ForEach-Object { '{0}  {1}' -f (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant(),$_.Name } | Set-Content -LiteralPath (Join-Path $dist 'SHA256SUMS.txt') -Encoding utf8
Write-Output $zip
