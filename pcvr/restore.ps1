param([ValidateSet('1.40.8','1.44.1')][string]$GameVersion='1.40.8')
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$cache=Join-Path $root '.cache/pcvr'
$destination=Join-Path $cache "refs-$GameVersion"
$target=(Get-Content (Join-Path $PSScriptRoot 'targets.json') -Raw | ConvertFrom-Json).$GameVersion
New-Item -ItemType Directory -Path $cache -Force | Out-Null
if(-not(Test-Path -LiteralPath (Join-Path $destination '.git'))) {
    & git clone --depth 1 --branch "version/$($target.referenceVersion)" https://github.com/beat-forge/beatsaber-stripped.git $destination
    if($LASTEXITCODE -ne 0){throw 'Could not fetch public compile-only game references.'}
}
$current=& git -C $destination rev-parse HEAD
if($current -ne $target.referenceCommit) {
    & git -C $destination fetch --depth 1 origin $target.referenceCommit
    if($LASTEXITCODE -ne 0){throw 'Could not fetch pinned game reference commit.'}
    & git -C $destination checkout --detach $target.referenceCommit
    if($LASTEXITCODE -ne 0){throw 'Could not select pinned game references.'}
}
$dependencies=(Get-Content (Join-Path $PSScriptRoot 'dependencies.lock.json') -Raw | ConvertFrom-Json).$GameVersion
foreach($dependency in $dependencies) {
    $archive=Join-Path $cache "$($dependency.id).zip"
    if(-not(Test-Path -LiteralPath $archive)){Invoke-WebRequest -Uri $dependency.url -OutFile $archive -TimeoutSec 90}
    $actual=(Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
    if($actual -ne $dependency.sha256){throw "Dependency checksum mismatch: $($dependency.name). Remove its cached ZIP and restore again."}
    Expand-Archive -LiteralPath $archive -DestinationPath $destination -Force
}
# BSIPA archives contain their reference assemblies in the installer payload.
$loader=Join-Path $destination 'IPA/Data/Managed/IPA.Loader.dll'
if(Test-Path -LiteralPath $loader){Copy-Item -LiteralPath $loader -Destination (Join-Path $destination 'Beat Saber_Data/Managed/IPA.Loader.dll') -Force}
$ipaLibs=Join-Path $destination 'IPA/Libs'
if(Test-Path -LiteralPath $ipaLibs){Get-ChildItem -LiteralPath $ipaLibs -File | Copy-Item -Destination (Join-Path $destination 'Libs') -Force}
Write-Output "Restored $GameVersion build references (game API $($target.referenceVersion)); $($dependencies.Count) verified dependency archives."
