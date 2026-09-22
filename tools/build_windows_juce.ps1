[CmdletBinding()]
param([string] $InstallPrefix = "$PSScriptRoot/../build-win-juce-install",
      [string] $SourceDir = "$PSScriptRoot/../build-win-juce-source",
      [string] $BuildDir = "$PSScriptRoot/../build-win-juce",
      [string] $SourceArchive = '', [int] $BuildJobs = 6)
$ErrorActionPreference = 'Stop'
. "$PSScriptRoot/windows_common.ps1"
Initialize-WindowsBuild
if (-not (Test-Path -LiteralPath "$SourceDir/CMakeLists.txt")) {
    if ($SourceArchive) {
        python "$PSScriptRoot/extract_source.py" $SourceArchive $SourceDir
        Assert-NativeSuccess 'JUCE archive extraction'
    } else {
        git clone --depth 1 --branch 8.0.14 https://github.com/juce-framework/JUCE.git $SourceDir
        Assert-NativeSuccess 'JUCE download'
    }
}
if (Test-Path -LiteralPath "$SourceDir/.git") {
    $revision = git -C $SourceDir rev-parse HEAD
    if ($revision -ne '2cdfca8feb300fb424002ba2c2751569e5bacb64') { throw 'JUCE source is not the pinned 8.0.14 revision.' }
}
cmake -S $SourceDir -B $BuildDir -G 'Visual Studio 17 2022' -A x64 "-DCMAKE_INSTALL_PREFIX=$InstallPrefix"
Assert-NativeSuccess 'JUCE configure'
cmake --build $BuildDir --config Release --target install --parallel $BuildJobs
Assert-NativeSuccess 'JUCE build'
