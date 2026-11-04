param(
    [ValidateSet('1.40.8','1.44.1')][string]$GameVersion='1.40.8',
    [string]$NativeBuild='',
    [string]$LlvmMingw='',
    [switch]$SkipRestore
)
$ErrorActionPreference='Stop'
$root=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if(-not $NativeBuild){$NativeBuild=Join-Path $root 'build/pcvr-native'}
$NativeBuild=[IO.Path]::GetFullPath($NativeBuild)
if(-not $SkipRestore){ & (Join-Path $PSScriptRoot 'restore.ps1') -GameVersion $GameVersion }
$cmake=(Get-Command cmake -ErrorAction SilentlyContinue).Source
if(-not $cmake){$cmake=Join-Path $root '.tools/cmake/bin/cmake.exe'}
if(-not(Test-Path -LiteralPath $cmake)){throw 'CMake 3.22 or newer is required.'}
$arguments=@('-S',(Join-Path $PSScriptRoot 'native'),'-B',$NativeBuild)
if($LlvmMingw) {
    $arguments+=@('-G','Ninja',"-DCMAKE_TOOLCHAIN_FILE=$(Join-Path $PSScriptRoot 'native/mingw-toolchain.cmake')","-DLLVM_MINGW=$LlvmMingw",'-DCMAKE_BUILD_TYPE=Release')
    $ninja=(Get-Command ninja -ErrorAction SilentlyContinue).Source
    if(-not $ninja){$ninja=Join-Path $root '.tools/qpm/ninja.exe'}
    $arguments+="-DCMAKE_MAKE_PROGRAM=$ninja"
} else { $arguments+=@('-G','Visual Studio 17 2022','-A','x64') }
foreach($dependency in @('curl','mbedtls')) {
    $source=Join-Path $root "build/_deps/rankedtools_$dependency-src"
    if(Test-Path -LiteralPath $source){$arguments+="-DFETCHCONTENT_SOURCE_DIR_RANKEDTOOLS_$($dependency.ToUpperInvariant())=$source"}
}
& $cmake @arguments
if($LASTEXITCODE -ne 0){throw 'Native engine configuration failed.'}
& $cmake --build $NativeBuild --config Release --parallel 4
if($LASTEXITCODE -ne 0){throw 'Native engine build failed.'}
$project=Join-Path $PSScriptRoot 'RankedTools.Pcvr/RankedTools.Pcvr.csproj'
& dotnet restore $project --locked-mode --packages (Join-Path $root '.cache/pcvr/nuget')
if($LASTEXITCODE -ne 0){throw '.NET restore failed.'}
& dotnet build $project -c Release --no-restore "-p:GameVersion=$GameVersion"
if($LASTEXITCODE -ne 0){throw 'PCVR plugin build failed.'}
