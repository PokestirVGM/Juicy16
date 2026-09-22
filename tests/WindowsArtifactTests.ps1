[CmdletBinding()]
param([Parameter(Mandatory)][string] $ArtifactsDir)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. "$PSScriptRoot/../tools/windows_common.ps1"
Initialize-WindowsBuild
$allowed = @('KERNEL32.dll','USER32.dll','GDI32.dll','ADVAPI32.dll','SHELL32.dll',
    'ole32.dll','OLEAUT32.dll','COMDLG32.dll','WINMM.dll','VERSION.dll','IMM32.dll',
    'SHLWAPI.dll','WS2_32.dll','WININET.dll','CRYPT32.dll','bcrypt.dll','UxTheme.dll',
    'DWMAPI.dll','DWrite.dll','D2D1.dll','D3D11.dll','DXGI.dll','dcomp.dll','WINHTTP.dll',
    'SETUPAPI.dll','HID.dll','COMCTL32.dll','MSIMG32.dll','PROPSYS.dll','ntdll.dll',
    'WINSPOOL.DRV','Normaliz.dll','DNSAPI.dll','IPHLPAPI.DLL','dbghelp.dll')
foreach ($relative in @('VST3/Juicy16.vst3/Contents/x86_64-win/Juicy16.vst3','Standalone/Juicy16.exe')) {
    $binary = Join-Path $ArtifactsDir $relative
    if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) { throw "Missing release binary: $binary" }
    $headers = & $script:WindowsDumpbin /headers $binary
    Assert-NativeSuccess 'PE headers'
    if (-not ($headers -match '8664 machine \(x64\)')) { throw "Not an x64 binary: $binary" }
    $dependencies = & $script:WindowsDumpbin /dependents $binary
    Assert-NativeSuccess 'PE dependency inspection'
    $imports = @($dependencies | ForEach-Object {
        if ($_ -match '^\s+([A-Za-z0-9_.-]+\.(?:dll|drv))\s*$') { $Matches[1] }
    })
    if ($imports.Count -eq 0) { throw "No imports found: $binary" }
    foreach ($dll in $imports) {
        if ($dll -notin $allowed -and $dll -notmatch '^api-ms-win-') {
            throw "Unexpected non-system runtime dependency in ${relative}: $dll"
        }
    }
    Write-Output "$relative : x64; system-only imports: $($imports -join ', ')"
    Write-Output "SHA256: $((Get-FileHash -LiteralPath $binary -Algorithm SHA256).Hash)"
}
$metadata = Join-Path $ArtifactsDir 'VST3/Juicy16.vst3/Contents/Resources/moduleinfo.json'
$module = Get-Content -LiteralPath $metadata -Raw | ConvertFrom-Json
if ($module.Name -ne 'Juicy16' -or @($module.Classes).Count -lt 2) { throw 'Invalid VST3 module metadata.' }
Write-Output "VST3 metadata: $($module.Name) $($module.Version)"
