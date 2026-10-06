param(
    [string] $Compiler = "",
    [string] $Distro = "docker-desktop",
    [string] $LinuxWorkspace = ""
)
$ErrorActionPreference = "Stop"
$workspace = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
if (-not $Compiler) {
    $ndkRoot = Join-Path $env:APPDATA "QPM-RS/ndk"
    $ndk = Get-ChildItem -LiteralPath $ndkRoot -Directory | Sort-Object Name -Descending | Select-Object -First 1
    if (-not $ndk) { throw "No QPM Android NDK found. Pass -Compiler with the installed clang++.exe path." }
    $Compiler = Join-Path $ndk.FullName "toolchains/llvm/prebuilt/windows-x86_64/bin/clang++.exe"
}
if (-not (Test-Path -LiteralPath $Compiler -PathType Leaf)) { throw "Compiler not found: $Compiler" }
if (-not $LinuxWorkspace) {
    $drive = $workspace.Substring(0, 1).ToLowerInvariant()
    $LinuxWorkspace = "/mnt/host/$drive" + $workspace.Substring(2).Replace('\', '/')
}
Push-Location $workspace
try {
    & $Compiler --target=x86_64-linux-android24 -std=c++20 -static -O1 -ffunction-sections -fdata-sections '-Wl,--gc-sections' -I include -I extern/includes -I extern/includes/beatsaber-hook/shared/rapidjson/include tests/recommendation_tests.cpp -o tests/recommendation_tests
    if ($LASTEXITCODE -ne 0) { throw "Regression test compilation failed." }
    & $Compiler --target=x86_64-linux-android24 -std=c++20 -static -O1 -ffunction-sections -fdata-sections '-Wl,--gc-sections' -I include -I extern/includes/beatsaber-hook/shared/rapidjson/include tests/playlist_writer_tests.cpp src/recommendation.cpp src/attempt_history.cpp -o tests/playlist_writer_tests
    if ($LASTEXITCODE -ne 0) { throw "Playlist writer test compilation failed." }
    # Shell quoting must preserve spaces and literal apostrophes in mount paths.
    $singleQuote = [char]39
    $doubleQuote = [char]34
    $escapedQuote = "$singleQuote$doubleQuote$singleQuote$doubleQuote$singleQuote"
    $quotedWorkspace = "'" + $LinuxWorkspace.Replace("'", $escapedQuote) + "'"
    & wsl -d $Distro -- sh -c "cd $quotedWorkspace && ./tests/recommendation_tests"
    if ($LASTEXITCODE -ne 0) { throw "Regression checks failed." }
    & wsl -d $Distro -- sh -c "cd $quotedWorkspace && ./tests/playlist_writer_tests"
    if ($LASTEXITCODE -ne 0) { throw "Playlist writer checks failed." }
} finally {
    Pop-Location
}
