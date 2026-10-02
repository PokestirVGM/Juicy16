<# Native Windows build and automated release gates. Any failed step is fatal. #>
[CmdletBinding()]
param(
    [string] $DepsPrefix = "$PSScriptRoot/../build-win-deps",
    [string] $JucePrefix = "$PSScriptRoot/../build-win-juce-install",
    [string] $BuildDir = "$PSScriptRoot/../build-win",
    [int] $BuildJobs = 6,
    [switch] $SkipDependencies,
    [switch] $SkipJuce,
    [ValidateSet('Release','Debug')][string] $Configuration = 'Release',
    [string] $SourceArchiveDir = ''
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. "$PSScriptRoot/windows_common.ps1"
Initialize-WindowsBuild
$repoRoot = [IO.Path]::GetFullPath("$PSScriptRoot/..")
$DepsPrefix = [IO.Path]::GetFullPath($DepsPrefix)
$JucePrefix = [IO.Path]::GetFullPath($JucePrefix)
$BuildDir = [IO.Path]::GetFullPath($BuildDir)
New-Item -ItemType Directory -Path $BuildDir -Force | Out-Null
Start-Transcript -Path "$BuildDir/verification-$Configuration.txt" -Force
try {
    Write-Output "Windows $Configuration verification; $([Environment]::OSVersion.VersionString)"
    if (-not $SkipDependencies) {
        & "$PSScriptRoot/build_windows_dependencies.ps1" -InstallPrefix $DepsPrefix `
            -WorkDir "$BuildDir-dependency-sources" -KeepSources -BuildJobs $BuildJobs `
            -Configuration $Configuration -SourceArchiveDir $SourceArchiveDir
    }
    if (-not $SkipJuce) {
        $juceArchive = ''
        if ($SourceArchiveDir) { $juceArchive = Join-Path $SourceArchiveDir 'JUCE-8.0.14.tar.gz' }
        & "$PSScriptRoot/build_windows_juce.ps1" -InstallPrefix $JucePrefix `
            -SourceArchive $juceArchive -BuildJobs $BuildJobs
    }
    cmake -S $repoRoot -B $BuildDir -G 'Visual Studio 17 2022' -A x64 `
        "-DCMAKE_PREFIX_PATH=$DepsPrefix;$JucePrefix" -DFLUIDSYNTH_LINK_STATIC=ON `
        -DBUILD_TESTING=ON `
        -DJUICYSF_RELEASE_VALIDATION=ON -DJUICYSF_COPY_PLUGIN_AFTER_BUILD=OFF `
        "-DJUICYSF_FONT_CORPUS=$DepsPrefix/share/juicy16-test-fixtures" `
        "-DJUICYSF_SF3_FIXTURE=$DepsPrefix/share/juicy16-test-fixtures/VintageDreamsWaves-v2.sf3" `
        -DJUICYSF_WARNINGS_AS_ERRORS=ON
    Assert-NativeSuccess 'Configure'
    cmake --build $BuildDir --config $Configuration --parallel $BuildJobs
    Assert-NativeSuccess 'Build'
    ctest --test-dir $BuildDir -C $Configuration --output-on-failure --no-tests=error
    Assert-NativeSuccess 'Automated tests'
    Write-Output 'All automated Windows gates passed. DAW and minimum-OS testing remain manual.'
} finally {
    Stop-Transcript
}
