[CmdletBinding()]
param([string] $BuildDir = "$PSScriptRoot/../build-win",
      [string] $DependencySources = "$PSScriptRoot/../build-win-dependency-sources",
      [string] $JuceSource = "$PSScriptRoot/../build-win-juce-source",
      [string] $Iscc = "$PSScriptRoot/../build-win-inno/ISCC.exe",
      [ValidatePattern('^BC[1-9][0-9]*$')][string] $Candidate = 'BC1')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. "$PSScriptRoot/../tools/windows_common.ps1"
Initialize-WindowsBuild
$repo = [IO.Path]::GetFullPath("$PSScriptRoot/..")
$BuildDir = [IO.Path]::GetFullPath($BuildDir)
$artifacts = Join-Path $BuildDir 'JuicySFPlugin_artefacts/Release'
if (-not (Test-Path -LiteralPath $Iscc)) { throw 'Pass -Iscc with the Inno Setup 6 compiler path.' }
$cmakeText = Get-Content -LiteralPath "$repo/CMakeLists.txt" -Raw
$version = [regex]::Match($cmakeText, 'project\(JUICY16 VERSION ([0-9.]+)\)').Groups[1].Value
$label = [regex]::Match($cmakeText, 'set\(JUICYSF_PRERELEASE_LABEL_DEFAULT "([^"]*)"\)').Groups[1].Value
if (-not $version) { throw 'Cannot determine product version.' }
if ($label) { $version += "-$label" }
$name = "Juicy16-$version-$Candidate-windows-x64"
$cache = Get-Content -LiteralPath "$BuildDir/CMakeCache.txt" -Raw
if ($cache -notmatch 'JUICYSF_RELEASE_VALIDATION:BOOL=ON') { throw 'Packaging requires a strict release build.' }
$pinnedLabel = [regex]::Match($cache, '(?m)^JUICYSF_PRERELEASE_LABEL:[^=]+=([^\r\n]*)')
if ($pinnedLabel.Success -and $pinnedLabel.Groups[1].Value -ne $label) {
    throw 'The build cache pins a different prerelease label from the source.'
}
ctest --test-dir $BuildDir -C Release --output-on-failure
Assert-NativeSuccess 'Release tests before packaging'
& "$repo/tests/WindowsArtifactTests.ps1" -ArtifactsDir $artifacts
$metadata = Get-Content "$artifacts/VST3/Juicy16.vst3/Contents/Resources/moduleinfo.json" -Raw | ConvertFrom-Json
if ($metadata.Version -ne $version.Split('-')[0]) { throw "Built version differs from $version" }
python "$PSScriptRoot/package_windows.py" --root $repo --artifacts $artifacts `
    --dependency-sources $DependencySources --juce-source $JuceSource --name $name
Assert-NativeSuccess 'Portable and source archives'
$stage = Join-Path $PSScriptRoot "out/$name-Portable"
cmake "-DSOURCE_ROOT=$stage" -P "$repo/tests/DocumentationLinkTests.cmake"
Assert-NativeSuccess 'Packaged documentation links'
& $Iscc "/DStageDir=$stage" "/DAppVersion=$version" "/DOutputDir=$PSScriptRoot/out" `
    "/DPackageName=$name" "$PSScriptRoot/windows.iss"
Assert-NativeSuccess 'Installer compilation'
$installer = Join-Path $PSScriptRoot "out/$name-Setup.exe"
"$((Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant())  $name-Setup.exe" |
    Set-Content -LiteralPath "$installer.sha256" -Encoding utf8

$verification = Join-Path $BuildDir "package-verification-$Candidate"
if (Test-Path -LiteralPath $verification) { throw "Package verification directory already exists: $verification" }
Expand-Archive -LiteralPath "$stage.zip" -DestinationPath $verification
$extracted = Join-Path $verification "$name-Portable"
foreach ($line in Get-Content -LiteralPath "$extracted/SHA256SUMS") {
    $expected, $relative = $line -split '  ', 2
    $actual = (Get-FileHash -LiteralPath (Join-Path $extracted $relative) -Algorithm SHA256).Hash
    if ($actual -ine $expected) { throw "Extracted file checksum mismatch: $relative" }
}
& "$repo/tests/WindowsArtifactTests.ps1" -ArtifactsDir $extracted
& "$BuildDir/Release/JuicySFVST3Smoke.exe" "$extracted/VST3/Juicy16.vst3" `
    'C:/Windows/System32/drivers/gm.dls' "$repo/tests/fixtures/vst3_multichannel_programs.csv"
Assert-NativeSuccess 'Extracted portable plugin smoke test'
Write-Output "Created and verified: $name (portable, installer, source, SHA-256 sidecars)"
