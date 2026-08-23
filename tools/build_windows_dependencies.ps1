<#
.SYNOPSIS
    Build the minimal static dependency closure used by the Juicy16 Windows Beta.

.DESCRIPTION
    The Windows counterpart of tools/build_macos_dependencies.sh. It builds the
    same pinned versions from the same checksummed upstream sources, so both
    platforms ship one dependency inventory rather than two.

    Everything is linked statically, including the Microsoft C runtime, so the
    resulting VST3 needs no Visual C++ redistributable on a tester's machine.
    The plugin build must use the same runtime; CMakeLists.txt sets it whenever
    FLUIDSYNTH_LINK_STATIC is on under MSVC.

    UNPROVEN: this recipe has not been executed. It is written from the macOS
    recipe and FluidSynth's documented Windows options, and every claim about
    the artifact it produces stays open in MILESTONE_PLAN.md Phase 4.3 until a
    real run and host validation.

.PARAMETER InstallPrefix
    Where to install the closure. Defaults to C:\juicy16-deps — deliberately
    short and outside the repository, because MSVC embeds __FILE__ paths in
    object code and has no -ffile-prefix-map equivalent.

.EXAMPLE
    pwsh -File tools/build_windows_dependencies.ps1
#>

[CmdletBinding()]
param(
    [string] $InstallPrefix = 'C:\juicy16-deps',
    [int] $BuildJobs = [Environment]::ProcessorCount
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

# Written for PowerShell 7 (`pwsh`), which is what the CI job uses, but the
# checks below avoid PowerShell 6+ only constructs so a Windows PowerShell 5.1
# run fails with a clear message rather than a StrictMode error.
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'This dependency recipe supports only Windows.'
}

# The recipe writes only build products here, but a mistyped prefix should not
# be able to scatter them across a system location.
$forbiddenPrefixes = @('C:\', 'C:\Windows', 'C:\Program Files', 'C:\Program Files (x86)', 'C:\Users')
$normalisedPrefix = ([IO.Path]::GetFullPath($InstallPrefix)).TrimEnd('\', '/')
foreach ($forbidden in $forbiddenPrefixes) {
    if ($normalisedPrefix -ieq $forbidden.TrimEnd('\', '/')) {
        throw "Refusing unsafe dependency install prefix: $InstallPrefix"
    }
}
if ($normalisedPrefix -match '\s') {
    throw "Dependency install prefix must not contain whitespace: $InstallPrefix"
}

$workDir = Join-Path ([IO.Path]::GetTempPath()) ("juicy16-deps-" + [Guid]::NewGuid().ToString('N').Substring(0, 8))
New-Item -ItemType Directory -Path $workDir -Force | Out-Null

try {
    function Get-PinnedSource {
        param(
            [Parameter(Mandatory)] [string] $Name,
            [Parameter(Mandatory)] [string] $Url,
            [Parameter(Mandatory)] [string] $ExpectedSha256
        )

        $archive = Join-Path $workDir "$Name.tar.gz"
        $sourceDir = Join-Path $workDir $Name

        Write-Host "-- fetching $Name"
        # Invoke-WebRequest honours redirects and TLS defaults; curl.exe is
        # present on Windows 10 1803+ but this avoids depending on it.
        Invoke-WebRequest -Uri $Url -OutFile $archive -UseBasicParsing -MaximumRedirection 5

        $actual = (Get-FileHash -Algorithm SHA256 -Path $archive).Hash.ToLowerInvariant()
        if ($actual -ne $ExpectedSha256.ToLowerInvariant()) {
            throw "Checksum verification failed for ${Name}: expected $ExpectedSha256, got $actual"
        }

        New-Item -ItemType Directory -Path $sourceDir -Force | Out-Null
        # bsdtar ships with Windows 10 1803+ and handles gzip and strip-components.
        & tar.exe -xzf $archive -C $sourceDir --strip-components=1
        if ($LASTEXITCODE -ne 0) { throw "Failed to extract $Name" }
    }

    function Build-AndInstall {
        param(
            [Parameter(Mandatory)] [string] $SourceDir,
            [Parameter(Mandatory)] [string] $BuildDir,
            [string[]] $CMakeArguments = @()
        )

        $configure = @(
            '-S', $SourceDir,
            '-B', $BuildDir,
            '-G', 'Visual Studio 17 2022',
            '-A', 'x64',
            "-DCMAKE_INSTALL_PREFIX=$normalisedPrefix",
            '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded',
            '-DCMAKE_POLICY_DEFAULT_CMP0091=NEW',
            # Windows 10 version 1607 is the approved API floor; see
            # MILESTONE_PLAN.md Phase 4.1 and CMakeLists.txt. CMake's own MSVC
            # defaults are repeated because setting these variables replaces them.
            '-DCMAKE_C_FLAGS=/DWIN32 /D_WINDOWS /DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00 /DNTDDI_VERSION=0x0A000002',
            '-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /DWINVER=0x0A00 /D_WIN32_WINNT=0x0A00 /DNTDDI_VERSION=0x0A000002'
        ) + $CMakeArguments

        & cmake @configure
        if ($LASTEXITCODE -ne 0) { throw "Configure failed for $SourceDir" }
        & cmake --build $BuildDir --config Release --target install --parallel $BuildJobs
        if ($LASTEXITCODE -ne 0) { throw "Build failed for $SourceDir" }
    }

    # Apply the reviewed libsndfile IRCAM hardening backport. Windows has no
    # guaranteed patch.exe, so this performs the same two edits as
    # vendor/libsndfile_patched/libsndfile-1.2.2-ircam-hardening.patch by exact
    # text substitution, bracketed by the pre- and post-edit file hashes. That is
    # stricter than applying a diff: the source must be byte-for-byte the pinned
    # upstream file, and the result must be byte-for-byte the reviewed one.
    # Hard failure - a closure that silently skipped a security patch must not
    # produce an artifact. See vendor/libsndfile_patched/README.md.
    function Repair-SndfileIrcam {
        param([Parameter(Mandatory)] [string] $SourceDir)

        $baseSha = '52fab7073b1c7716902ee217769a48117577c1f33e84fb038232e2fe41088470'
        $patchedSha = '27c25a5938d0c2571f9aaf0910ecedee57c440e62be66cf55f7708fa5ba3a1ab'
        $file = Join-Path $SourceDir 'src\ircam.c'

        $actual = (Get-FileHash -Algorithm SHA256 -Path $file).Hash.ToLowerInvariant()
        if ($actual -ne $baseSha) {
            throw "src/ircam.c is not the pinned libsndfile 1.2.2 file: expected $baseSha, got $actual"
        }

        # ReadAllText/WriteAllText leave the file's LF endings untouched.
        $text = [IO.File]::ReadAllText($file)
        # CVE-2025-52194: an out-of-range or NaN float is UB through a C cast.
        $text = $text.Replace('(int) samplerate', 'psf_lrintf (samplerate)')
        # A zero or negative channel count passes 1.2.2's upper-bound-only check.
        $text = $text.Replace(
            'psf->sf.channels > SF_MAX_CHANNELS',
            'psf->sf.channels < 1 || psf->sf.channels > SF_MAX_CHANNELS')
        [IO.File]::WriteAllText($file, $text, (New-Object Text.UTF8Encoding $false))

        $actual = (Get-FileHash -Algorithm SHA256 -Path $file).Hash.ToLowerInvariant()
        if ($actual -ne $patchedSha) {
            throw "libsndfile IRCAM hardening produced an unexpected src/ircam.c: expected $patchedSha, got $actual"
        }
        Write-Host '-- patched sndfile (IRCAM hardening)'
    }

    # Versions and checksums are identical to tools/build_macos_dependencies.sh.
    # Change them in both places or the two platforms stop sharing one inventory.
    Get-PinnedSource -Name 'fluidsynth' `
        -Url 'https://github.com/FluidSynth/fluidsynth/archive/refs/tags/v2.5.5.tar.gz' `
        -ExpectedSha256 '0827eefc06f66157c332d7bd0d65ee81be5d4c795f214db7ba0e1c70ee394430'
    Get-PinnedSource -Name 'gcem' `
        -Url 'https://github.com/kthohr/gcem/archive/012ae73c6d0a2cb09ffe86475f5c6fba3926e200.tar.gz' `
        -ExpectedSha256 '34ab0ee87a9eb26d3087fa9b49c2572ea8ee03db0c9705b83648301a3a3fc172'
    Get-PinnedSource -Name 'ogg' `
        -Url 'https://github.com/xiph/ogg/archive/refs/tags/v1.3.6.tar.gz' `
        -ExpectedSha256 '95b643da661155d79db9de2fca55daed3a8d491039829def246aacb3d9201c81'
    Get-PinnedSource -Name 'vorbis' `
        -Url 'https://github.com/xiph/vorbis/archive/refs/tags/v1.3.7.tar.gz' `
        -ExpectedSha256 '270c76933d0934e42c5ee0a54a36280e2d87af1de3cc3e584806357e237afd13'
    Get-PinnedSource -Name 'flac' `
        -Url 'https://github.com/xiph/flac/archive/refs/tags/1.5.0.tar.gz' `
        -ExpectedSha256 'aea54ed186ad07a34750399cb27fc216a2b62d0ffcd6dc2e3064a3518c3146f8'
    Get-PinnedSource -Name 'opus' `
        -Url 'https://downloads.xiph.org/releases/opus/opus-1.6.1.tar.gz' `
        -ExpectedSha256 '6ffcb593207be92584df15b32466ed64bbec99109f007c82205f0194572411a1'
    Get-PinnedSource -Name 'sndfile' `
        -Url 'https://github.com/libsndfile/libsndfile/archive/refs/tags/1.2.2.tar.gz' `
        -ExpectedSha256 'ffe12ef8add3eaca876f04087734e6e8e029350082f3251f565fa9da55b52121'

    Repair-SndfileIrcam -SourceDir (Join-Path $workDir 'sndfile')

    # FluidSynth vendors GCEM in-tree rather than finding it.
    $gcemTarget = Join-Path $workDir 'fluidsynth\gcem'
    New-Item -ItemType Directory -Path $gcemTarget -Force | Out-Null
    & cmake -E copy_directory (Join-Path $workDir 'gcem') $gcemTarget
    if ($LASTEXITCODE -ne 0) { throw 'Failed to stage GCEM into the FluidSynth tree' }

    New-Item -ItemType Directory -Path $normalisedPrefix -Force | Out-Null

    Build-AndInstall -SourceDir (Join-Path $workDir 'ogg') `
        -BuildDir (Join-Path $workDir 'build-ogg') -CMakeArguments @(
        '-DBUILD_SHARED_LIBS=OFF',
        '-DINSTALL_DOCS=OFF')

    Build-AndInstall -SourceDir (Join-Path $workDir 'vorbis') `
        -BuildDir (Join-Path $workDir 'build-vorbis') -CMakeArguments @(
        '-DCMAKE_POLICY_VERSION_MINIMUM=3.5',
        "-DCMAKE_PREFIX_PATH=$normalisedPrefix",
        '-DBUILD_SHARED_LIBS=OFF')

    Build-AndInstall -SourceDir (Join-Path $workDir 'flac') `
        -BuildDir (Join-Path $workDir 'build-flac') -CMakeArguments @(
        "-DCMAKE_PREFIX_PATH=$normalisedPrefix",
        '-DCMAKE_DISABLE_FIND_PACKAGE_Intl=TRUE',
        '-DBUILD_SHARED_LIBS=OFF',
        '-DBUILD_CXXLIBS=OFF',
        '-DBUILD_PROGRAMS=OFF',
        '-DBUILD_EXAMPLES=OFF',
        '-DBUILD_TESTING=OFF',
        '-DBUILD_DOCS=OFF',
        '-DINSTALL_MANPAGES=OFF',
        '-DWITH_OGG=ON')

    Build-AndInstall -SourceDir (Join-Path $workDir 'opus') `
        -BuildDir (Join-Path $workDir 'build-opus') -CMakeArguments @(
        '-DOPUS_BUILD_SHARED_LIBRARY=OFF',
        '-DOPUS_BUILD_TESTING=OFF',
        '-DOPUS_BUILD_PROGRAMS=OFF')

    Build-AndInstall -SourceDir (Join-Path $workDir 'sndfile') `
        -BuildDir (Join-Path $workDir 'build-sndfile') -CMakeArguments @(
        '-DCMAKE_POLICY_VERSION_MINIMUM=3.5',
        "-DCMAKE_PREFIX_PATH=$normalisedPrefix",
        '-DCMAKE_DISABLE_FIND_PACKAGE_mp3lame=TRUE',
        '-DCMAKE_DISABLE_FIND_PACKAGE_mpg123=TRUE',
        '-DBUILD_SHARED_LIBS=OFF',
        '-DBUILD_PROGRAMS=OFF',
        '-DBUILD_EXAMPLES=OFF',
        '-DBUILD_TESTING=OFF',
        '-DENABLE_CPACK=OFF',
        '-DENABLE_EXTERNAL_LIBS=ON',
        '-DENABLE_MPEG=OFF')

    # The Windows audio and MIDI drivers are switched off for the same reason the
    # Core Audio ones are on macOS: Juicy16 only ever renders blocks handed to it
    # by a host, and an unused driver is one more thing to link and disclose.
    Build-AndInstall -SourceDir (Join-Path $workDir 'fluidsynth') `
        -BuildDir (Join-Path $workDir 'build-fluidsynth') -CMakeArguments @(
        "-DCMAKE_PREFIX_PATH=$normalisedPrefix",
        '-DDEFAULT_SOUNDFONT=',
        '-DBUILD_SHARED_LIBS=OFF',
        '-Dosal=cpp11',
        '-Denable-native-dls=ON',
        '-Denable-libinstpatch=OFF',
        '-Denable-libsndfile=ON',
        '-Denable-aufile=OFF',
        '-Denable-network=OFF',
        '-Denable-readline=OFF',
        '-Denable-dbus=OFF',
        '-Denable-dsound=OFF',
        '-Denable-wasapi=OFF',
        '-Denable-waveout=OFF',
        '-Denable-winmidi=OFF',
        '-Denable-sdl3=OFF',
        '-Denable-portaudio=OFF',
        '-Denable-openmp=OFF')

    # FluidSynth's native C++17 DLS loader is the approved Windows DLS path, and
    # its absence would not fail the build — the plugin would simply refuse every
    # DLS bank at runtime. Fail here instead, where the cause is obvious.
    $dlsObject = Get-ChildItem -Path (Join-Path $workDir 'build-fluidsynth') `
        -Recurse -Filter 'fluid_dls*.obj' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -eq $dlsObject) {
        throw 'FluidSynth built without the native DLS loader; DLS banks would not load. Check -Denable-native-dls.'
    }

    # FluidSynth's explicitly licensed SF3 regression input, kept beside the
    # closure. Strict validation loads it; packaging never stages from here.
    $fixtureDir = Join-Path $normalisedPrefix 'share\juicy16-test-fixtures'
    New-Item -ItemType Directory -Path $fixtureDir -Force | Out-Null
    Copy-Item (Join-Path $workDir 'fluidsynth\sf2\VintageDreamsWaves-v2.sf3') $fixtureDir -Force
    Copy-Item (Join-Path $workDir 'fluidsynth\sf2\COPYRIGHT.txt') `
        (Join-Path $fixtureDir 'VintageDreamsWaves-COPYRIGHT.txt') -Force

    Write-Host ''
    Write-Host "Built Windows x64 static dependencies at: $normalisedPrefix"
    Write-Host "Configure Juicy16 with -DCMAKE_PREFIX_PATH=`"$normalisedPrefix`" -DFLUIDSYNTH_LINK_STATIC=ON"
    Write-Host "Use -DJUICYSF_SF3_FIXTURE=`"$fixtureDir\VintageDreamsWaves-v2.sf3`" for strict validation."
}
finally {
    Remove-Item -Recurse -Force $workDir -ErrorAction SilentlyContinue
}
