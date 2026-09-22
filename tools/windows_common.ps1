# Shared native build-tool discovery. Also remove case-duplicate PATH entries
# inherited from some launchers: .NET Framework MSBuild rejects those entries.
function Initialize-WindowsBuild {
    $buildSearchPath = $env:PATH
    Remove-Item Env:PATH -ErrorAction SilentlyContinue
    Remove-Item Env:Path -ErrorAction SilentlyContinue
    $env:Path = $buildSearchPath
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path -LiteralPath $vswhere)) { throw 'Visual Studio 2022 C++ Build Tools are required.' }
    $vs = & $vswhere -latest -version '[17.0,18.0)' -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vs) { throw 'Install the Visual Studio 2022 Desktop development with C++ workload.' }
    if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
        $env:Path = "$vs\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;$env:Path"
    }
    $compiler = Get-ChildItem -LiteralPath "$vs\VC\Tools\MSVC" -Directory | Sort-Object Name -Descending | Select-Object -First 1
    $script:WindowsDumpbin = Join-Path $compiler.FullName 'bin\Hostx64\x64\dumpbin.exe'
    if (-not (Test-Path -LiteralPath $script:WindowsDumpbin)) { throw 'The x64 compiler tools are missing.' }
}

function Assert-NativeSuccess([string] $Step) {
    if ($LASTEXITCODE -ne 0) { throw "$Step failed (exit $LASTEXITCODE)." }
}
