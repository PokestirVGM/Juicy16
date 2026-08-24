<#
.SYNOPSIS
    One-shot Windows verification for Juicy16, capturing everything Phase 4.3 of
    ROADMAP.md needs into a single pasteable report.

.DESCRIPTION
    Runs the whole Windows path end to end: the pinned dependency closure, a
    clean configure and build, the test suite, and then the evidence Phase 4.3
    asks for - binary architecture, runtime DLL dependencies, VST3 module layout,
    the DLS capability probe, and artifact hashes.

    It deliberately does NOT stop at the first failure. Nothing here has ever
    executed, so a run that stops early wastes the trip; every step records its
    outcome and the script continues, then prints a summary of what passed.

    The transcript is written to verify-windows-report.txt in the repository
    root. Paste that file back.

.EXAMPLE
    pwsh -File tools/verify_windows.ps1
    pwsh -File tools/verify_windows.ps1 -SkipDependencies   # reuse an existing closure
#>

[CmdletBinding()]
param(
    [string] $DepsPrefix = 'C:\juicy16-deps',
    [string] $JucePrefix = 'C:\juicydeps',
    [string] $BuildDir = 'build-win',
    [int] $BuildJobs = [Environment]::ProcessorCount,
    [switch] $SkipDependencies
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Continue'

$repoRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $repoRoot
$report = Join-Path $repoRoot 'verify-windows-report.txt'
Remove-Item $report -ErrorAction SilentlyContinue

$results = [ordered]@{}

function Write-Report {
    param([string] $Text = '')
    Write-Host $Text
    Add-Content -Path $script:report -Value $Text
}

function Invoke-Step {
    param(
        [Parameter(Mandatory)] [string] $Name,
        [Parameter(Mandatory)] [scriptblock] $Action
    )
    Write-Report ''
    Write-Report ('=' * 78)
    Write-Report "STEP: $Name"
    Write-Report ('=' * 78)
    $ok = $false
    try {
        $output = & $Action 2>&1 | Out-String
        Write-Report $output.TrimEnd()
        $ok = ($LASTEXITCODE -eq 0 -or $null -eq $LASTEXITCODE)
    } catch {
        Write-Report "EXCEPTION: $_"
        $ok = $false
    }
    $script:results[$Name] = $ok
    Write-Report ">>> $Name : $(if ($ok) { 'OK' } else { 'FAILED' })"
}

Write-Report "Juicy16 Windows verification"
Write-Report "date          : $(Get-Date -Format o)"
Write-Report "repo          : $repoRoot"
Write-Report "commit        : $(git rev-parse HEAD 2>$null)"
Write-Report "worktree dirty: $(if (git status --porcelain) { 'YES - report which files' } else { 'no' })"
Write-Report "os            : $([Environment]::OSVersion.VersionString)"
Write-Report "powershell    : $($PSVersionTable.PSVersion)"
Write-Report "cmake         : $((cmake --version 2>&1 | Select-Object -First 1))"

if (-not $SkipDependencies) {
    Invoke-Step 'Build the pinned dependency closure' {
        & "$repoRoot\tools\build_windows_dependencies.ps1" -InstallPrefix $DepsPrefix -BuildJobs $BuildJobs
    }
} else {
    Write-Report ''
    Write-Report "Skipping the dependency build; reusing $DepsPrefix"
}

Invoke-Step 'Configure' {
    cmake -S . -B $BuildDir -G "Visual Studio 17 2022" -A x64 `
        "-DCMAKE_PREFIX_PATH=$DepsPrefix;$JucePrefix" `
        -DFLUIDSYNTH_LINK_STATIC=ON `
        "-DJUICYSF_SF3_FIXTURE=$DepsPrefix/share/juicy16-test-fixtures/VintageDreamsWaves-v2.sf3" `
        -DJUICYSF_COPY_PLUGIN_AFTER_BUILD=OFF `
        -DJUICYSF_WARNINGS_AS_ERRORS=ON
}

Invoke-Step 'Build (Release)' {
    cmake --build $BuildDir --config Release --parallel $BuildJobs
}

Invoke-Step 'Tests' {
    ctest --test-dir $BuildDir -C Release --output-on-failure
}

# DLS is the product's headline format and the one capability that cannot be
# inferred from a successful build: FluidSynth compiled without its native DLS
# loader produces a plugin that builds, loads, and refuses every DLS bank.
Invoke-Step 'DLS capability probe against the Windows system bank' {
    $probe = Get-ChildItem -Path $BuildDir -Recurse -Filter 'JuicySFFontQA.exe' `
        -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $probe) { Write-Output 'JuicySFFontQA.exe was not built'; $global:LASTEXITCODE = 1; return }
    $gm = 'C:\Windows\System32\drivers\gm.dls'
    if (-not (Test-Path $gm)) { Write-Output "No system DLS at $gm"; $global:LASTEXITCODE = 1; return }
    & $probe.FullName $gm
}

Invoke-Step 'VST3 module layout' {
    $vst3 = Get-ChildItem -Path $BuildDir -Recurse -Filter 'Juicy16.vst3' -Directory `
        -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $vst3) { Write-Output 'No Juicy16.vst3 bundle was produced'; $global:LASTEXITCODE = 1; return }
    Write-Output "bundle: $($vst3.FullName)"
    # Phase 4.3 requires the Contents/x86_64-win/ layout.
    Get-ChildItem -Path $vst3.FullName -Recurse -File |
        ForEach-Object { $_.FullName.Substring($vst3.FullName.Length) }
    $expected = Join-Path $vst3.FullName 'Contents\x86_64-win'
    if (Test-Path $expected) { Write-Output "OK: Contents/x86_64-win present" }
    else { Write-Output "MISSING: Contents/x86_64-win"; $global:LASTEXITCODE = 1 }
}

# A static closure plus the static CRT should leave only Windows system DLLs.
# Anything else is a missing-dependency failure on a tester's machine.
Invoke-Step 'Runtime DLL dependencies and architecture' {
    $binary = Get-ChildItem -Path $BuildDir -Recurse -Filter 'Juicy16.vst3' -File `
        -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $binary) { Write-Output 'No Juicy16.vst3 binary found'; $global:LASTEXITCODE = 1; return }
    Write-Output "binary: $($binary.FullName)"
    Write-Output "sha256: $((Get-FileHash -Algorithm SHA256 $binary.FullName).Hash)"
    $dumpbin = Get-ChildItem -Path 'C:\Program Files*\Microsoft Visual Studio' -Recurse `
        -Filter 'dumpbin.exe' -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -match 'Hostx64\\x64' } | Select-Object -First 1
    if ($null -eq $dumpbin) {
        Write-Output 'dumpbin.exe not found; run this from a Developer PowerShell for VS 2022'
        $global:LASTEXITCODE = 1
        return
    }
    & $dumpbin.FullName /headers $binary.FullName | Select-String -Pattern 'machine|magic'
    & $dumpbin.FullName /dependents $binary.FullName
}

Write-Report ''
Write-Report ('=' * 78)
Write-Report 'SUMMARY'
Write-Report ('=' * 78)
foreach ($entry in $results.GetEnumerator()) {
    Write-Report ("{0,-8} {1}" -f $(if ($entry.Value) { 'OK' } else { 'FAILED' }), $entry.Key)
}
$failed = ($results.Values | Where-Object { -not $_ }).Count
Write-Report ''
Write-Report "$failed step(s) failed."
Write-Report ''
Write-Report "Report written to: $report"
Write-Report 'Paste that file back. Every Windows claim in ROADMAP.md stays'
Write-Report 'marked unproven until it shows a clean run.'
