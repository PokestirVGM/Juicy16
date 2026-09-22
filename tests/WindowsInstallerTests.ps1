[CmdletBinding()]
param([Parameter(Mandatory)][string] $Installer,
      [Parameter(Mandatory)][string] $PortableDir,
      [string] $BuildDir = "$PSScriptRoot/../build-win",
      [string] $TestDirectory = '')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. "$PSScriptRoot/../tools/windows_common.ps1"
if (-not $TestDirectory) { $TestDirectory = Join-Path $BuildDir 'installer-roundtrip' }
$testRoot = [IO.Path]::GetFullPath($TestDirectory)
if (Test-Path -LiteralPath $testRoot) { throw "Test directory must be new: $testRoot" }
$uninstallKey = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\{D5CF3721-60F4-4894-8C73-6F0697F14414}_is1'
if (Test-Path $uninstallKey) { throw 'A per-user Juicy16 installation already exists; refusing to alter it.' }
New-Item -ItemType Directory -Path $testRoot | Out-Null
$app = Join-Path $testRoot 'Application'
$plugins = Join-Path $testRoot 'Plugins'
foreach ($pass in 1,2) {
    $arguments = @('/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART','/CURRENTUSER','/NOICONS',
        "/DIR=`"$app`"", "/LOG=`"$testRoot/install-$pass.log`"")
    # The upgrade must remember a custom scan folder even without a CLI override.
    if ($pass -eq 1) { $arguments += "/VST3DIR=`"$plugins`"" }
    $process = Start-Process -FilePath $Installer -ArgumentList $arguments -WindowStyle Hidden -PassThru -Wait
    if ($process.ExitCode -ne 0) { throw "Installer pass $pass failed: $($process.ExitCode)" }
    foreach ($pair in @(
        @("$PortableDir/VST3/Juicy16.vst3/Contents/x86_64-win/Juicy16.vst3", "$plugins/Juicy16.vst3/Contents/x86_64-win/Juicy16.vst3"),
        @("$PortableDir/Standalone/Juicy16.exe", "$app/Standalone/Juicy16.exe"))) {
        if ((Get-FileHash -LiteralPath $pair[0]).Hash -ne (Get-FileHash -LiteralPath $pair[1]).Hash) {
            throw 'Installed payload differs from tested portable build.'
        }
    }
}
& "$BuildDir/Release/JuicySFVST3Smoke.exe" "$plugins/Juicy16.vst3" `
    'C:/Windows/System32/drivers/gm.dls' "$PSScriptRoot/fixtures/vst3_multichannel_programs.csv"
Assert-NativeSuccess 'Installed VST3 smoke test'
'user-owned file' | Set-Content -LiteralPath "$app/preserve-me.txt"
$uninstaller = Join-Path $app 'unins000.exe'
$process = Start-Process -FilePath $uninstaller -ArgumentList @('/VERYSILENT','/SUPPRESSMSGBOXES',
    '/NORESTART', "/LOG=`"$testRoot/uninstall.log`"") -WindowStyle Hidden -PassThru -Wait
if ($process.ExitCode -ne 0) { throw "Uninstaller failed: $($process.ExitCode)" }
if ((Test-Path -LiteralPath "$plugins/Juicy16.vst3") -or
    (Test-Path -LiteralPath "$app/Standalone/Juicy16.exe") -or (Test-Path $uninstallKey)) {
    throw 'Uninstall left installed payload or registration behind.'
}
if (-not (Test-Path -LiteralPath "$app/preserve-me.txt")) { throw 'Uninstall deleted an unrelated user file.' }
Write-Output 'PASS: silent install, upgrade, exact payload hashes, installed VST3 smoke, uninstall, user-file preservation.'
