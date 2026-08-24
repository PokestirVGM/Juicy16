# cmake -B build -DCMAKE_INSTALL_PREFIX="$HOME/juicydeps" -DCMAKE_BUILD_TYPE=Debug
# cmake --build build

# To get started on a new plugin, copy this entire folder (containing this file and C++ sources) to
# a convenient location, and then start making modifications.

# The first line of any CMake project should be a call to `cmake_minimum_required`, which checks
# that the installed CMake will be able to understand the following CMakeLists, and ensures that
# CMake's behaviour is compatible with the named version. This is a standard CMake command, so more
# information can be found in the CMake docs.

cmake_minimum_required(VERSION 3.15)

# The top-level CMakeLists.txt file for a project must contain a literal, direct call to the
# `project()` command. `project()` sets up some helpful variables that describe source/binary
# directories, and the current project version. This is a standard CMake command.

project(JUICY16 VERSION 0.6.0)
# Deliberately NOT a plain CACHE default. A cached default is written once and
# then ignored, so bumping the label here left every existing build directory
# building - and displaying - the label it was first configured with. The version
# in the status bar is how the user tells a rebuilt plugin from a DAW's cached
# copy, so a stale one is worse than none. Editing this line now wins everywhere;
# -DJUICYSF_PRERELEASE_LABEL=... on the command line still overrides it, and keeps
# overriding it for that build directory until the cache entry is removed.
set(JUICYSF_PRERELEASE_LABEL_DEFAULT "alpha.3")
if (NOT DEFINED JUICYSF_PRERELEASE_LABEL)
  set(JUICYSF_PRERELEASE_LABEL "${JUICYSF_PRERELEASE_LABEL_DEFAULT}")
endif ()
if (JUICYSF_PRERELEASE_LABEL)
  set(JUICYSF_DISPLAY_VERSION "${PROJECT_VERSION}-${JUICYSF_PRERELEASE_LABEL}")
else ()
  set(JUICYSF_DISPLAY_VERSION "${PROJECT_VERSION}")
endif ()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# If you've installed JUCE somehow (via a package manager, or directly using the CMake install
# target), you'll need to tell this project that it depends on the installed copy of JUCE. If you've
# included JUCE directly in your source tree (perhaps as a submodule), you'll need to tell CMake to
# include that subdirectory as part of the build.

find_package(JUCE 8.0.14 EXACT CONFIG REQUIRED)
# or
# add_subdirectory(JUCE)                    # If you've put JUCE in a subdirectory called JUCE

# default to static libraries, since distribution is easier
option(BUILD_SHARED_LIBS "Build using shared libraries" off)
# separate option to control fluidsynth linking independent of JUCE module compilation
option(FLUIDSYNTH_LINK_STATIC "Link FluidSynth statically (requires a libfluidsynth.a)" off)
option(JUICYSF_COPY_PLUGIN_AFTER_BUILD
       "Install built plugins into the current user's plugin directories" off)
option(JUICYSF_RELEASE_VALIDATION
       "Reject configuration that lacks approved portable-release inputs" off)
set(JUICYSF_FONT_CORPUS "" CACHE PATH
    "Optional private/local SF2/SF3/DLS corpus path registered with CTest; never packaged")
set(JUICYSF_SF3_FIXTURE "" CACHE FILEPATH
    "Licensed local SF3 file required by strict release validation; never packaged")
set(JUICYSF_CODE_SIGN_IDENTITY "-" CACHE STRING
    "macOS code-sign identity used after bundle resources and VST3 metadata are finalized; empty disables signing")
set(JUICYSF_MACOS_SYSTEM_DLS
    "/System/Library/Components/CoreAudio.component/Contents/Resources/gs_instruments.dls")
# Windows ships the Microsoft GS Wavetable DLS bank in the same spirit: a real
# DLS a clean checkout can parse without adding a copyrighted fixture to the
# repository. It is read in place and never copied or packaged.
set(JUICYSF_WINDOWS_SYSTEM_DLS "C:/Windows/System32/drivers/gm.dls")
if (APPLE)
  set(JUICYSF_SYSTEM_DLS "${JUICYSF_MACOS_SYSTEM_DLS}")
elseif (WIN32)
  set(JUICYSF_SYSTEM_DLS "${JUICYSF_WINDOWS_SYSTEM_DLS}")
else ()
  set(JUICYSF_SYSTEM_DLS "")
endif ()
include(CTest)

option(JUICYSF_WARNINGS_AS_ERRORS
       "Fail the build on a warning in first-party sources (CI quality gate)" off)

# JUCE modules and the bundled Steinberg VST3 SDK compile into our own targets,
# so the policy is applied per source file rather than per target.
function(juicysf_treat_warnings_as_errors)
  if (NOT JUICYSF_WARNINGS_AS_ERRORS)
    return()
  endif ()
  if (MSVC)
    set(_flag /WX)
  else ()
    set(_flag -Werror)
  endif ()
  foreach (_source IN LISTS ARGN)
    set_property(SOURCE "${_source}" APPEND PROPERTY COMPILE_OPTIONS "${_flag}")
  endforeach ()
endfunction()

if (JUICYSF_RELEASE_VALIDATION)
  if (NOT BUILD_TESTING)
    message(FATAL_ERROR
      "Beta 1 release validation requires BUILD_TESTING=ON so DLS and artifact capability gates cannot be skipped")
  endif ()
  if (BUILD_SHARED_LIBS)
    message(FATAL_ERROR "Release validation requires BUILD_SHARED_LIBS=OFF")
  endif ()
  if (NOT FLUIDSYNTH_LINK_STATIC)
    message(FATAL_ERROR "Release validation requires FLUIDSYNTH_LINK_STATIC=ON")
  endif ()
  if (APPLE)
    if (NOT CMAKE_OSX_DEPLOYMENT_TARGET VERSION_EQUAL "11.0")
      message(FATAL_ERROR
        "Beta 1 release validation requires CMAKE_OSX_DEPLOYMENT_TARGET=11.0")
    endif ()
    if (NOT CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
      message(FATAL_ERROR
        "Beta 1 release validation supports only CMAKE_OSX_ARCHITECTURES=arm64")
    endif ()
    if (NOT EXISTS "${JUICYSF_MACOS_SYSTEM_DLS}")
      message(FATAL_ERROR
        "Beta 1 macOS release validation requires a real DLS input for the runtime capability gate: ${JUICYSF_MACOS_SYSTEM_DLS}")
    endif ()
  elseif (WIN32)
    if (NOT MSVC)
      message(FATAL_ERROR "Beta 1 Windows release validation requires MSVC")
    endif ()
    if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
      message(FATAL_ERROR "Beta 1 Windows release validation supports only x86_64")
    endif ()
    if (NOT JUICYSF_FONT_CORPUS OR NOT EXISTS "${JUICYSF_FONT_CORPUS}")
      message(FATAL_ERROR
        "Beta 1 Windows release validation requires JUICYSF_FONT_CORPUS to contain a private/local DLS test file")
    endif ()
    file(GLOB_RECURSE JUICYSF_RELEASE_DLS_FILES LIST_DIRECTORIES FALSE
      "${JUICYSF_FONT_CORPUS}/*.dls" "${JUICYSF_FONT_CORPUS}/*.DLS")
    if (NOT JUICYSF_RELEASE_DLS_FILES)
      message(FATAL_ERROR
        "Beta 1 Windows release validation found no .dls file under JUICYSF_FONT_CORPUS=${JUICYSF_FONT_CORPUS}")
    endif ()
    list(GET JUICYSF_RELEASE_DLS_FILES 0 JUICYSF_RELEASE_DLS_PROBE)
  else ()
    message(FATAL_ERROR
      "Beta 1 release validation supports only macOS arm64 and Windows x86_64")
  endif ()
endif ()



# How FluidSynth is found. pkg-config is the macOS path and produced every
# recorded macOS result. MSVC has no pkg-config by default, while the pinned
# Windows closure installs FluidSynth's own CMake package config, so Windows uses
# that instead. The option is deliberately not Windows-only: it can be switched
# on for a macOS build so the config path is exercised on the platform where the
# test suite actually runs, rather than shipping a code path nothing executes.
if (MSVC)
  set(_juicysf_fluidsynth_config_default ON)
else ()
  set(_juicysf_fluidsynth_config_default OFF)
endif ()
option(JUICYSF_FLUIDSYNTH_CMAKE_CONFIG
  "Find FluidSynth through its installed CMake package config instead of pkg-config"
  ${_juicysf_fluidsynth_config_default})

# The macOS strict checks below read FLUIDSYNTH_STATIC_LINK_LIBRARIES to verify
# every static archive's architecture and deployment target. The CMake package
# config does not expose that list, so those checks would silently pass over an
# empty list. Refuse the combination rather than weaken the gate.
if (JUICYSF_RELEASE_VALIDATION AND APPLE AND JUICYSF_FLUIDSYNTH_CMAKE_CONFIG)
  message(FATAL_ERROR
    "macOS release validation requires the pkg-config discovery path, because "
    "its per-archive architecture and deployment-target checks read "
    "FLUIDSYNTH_STATIC_LINK_LIBRARIES. Configure with "
    "-DJUICYSF_FLUIDSYNTH_CMAKE_CONFIG=OFF.")
endif ()

if (JUICYSF_FLUIDSYNTH_CMAKE_CONFIG)
  if (FLUIDSYNTH_LINK_STATIC)
    find_package(FluidSynth CONFIG REQUIRED COMPONENTS static)
  else ()
    find_package(FluidSynth CONFIG REQUIRED)
  endif ()
  set(FLUIDSYNTH_VERSION "${FluidSynth_VERSION}")
  # <prefix>/lib/cmake/fluidsynth -> <prefix>, matching what pkg-config reports.
  get_filename_component(FLUIDSYNTH_PREFIX "${FluidSynth_DIR}/../../.." ABSOLUTE)
else ()
  find_package(PkgConfig REQUIRED)

  set(PKG_CONFIG_USE_CMAKE_PREFIX_PATH off)

  # pkg-config emits unquoted -L/-I flags. A space in the source or build path is
  # split into two arguments, after which the pinned dependency prefix is dropped
  # and FluidSynth silently resolves from somewhere else (typically Homebrew).
  if (JUICYSF_RELEASE_VALIDATION)
    foreach (_path IN ITEMS "${CMAKE_SOURCE_DIR}" "${CMAKE_BINARY_DIR}")
      if (_path MATCHES "[ \t]")
        message(FATAL_ERROR
          "Release validation requires a path without whitespace, but found: ${_path}\n"
          "pkg-config cannot quote such a path, so the pinned dependency prefix "
          "would be silently discarded. Build the candidate from a space-free location.")
      endif ()
    endforeach ()
  endif ()

  pkg_search_module(FLUIDSYNTH REQUIRED IMPORTED_TARGET fluidsynth>=2)
endif ()
if (JUICYSF_RELEASE_VALIDATION AND NOT FLUIDSYNTH_VERSION VERSION_EQUAL "2.5.5")
  message(FATAL_ERROR
    "Beta 1 release validation requires FluidSynth 2.5.5 exactly; found ${FLUIDSYNTH_VERSION}. "
    "Use the pinned dependency recipe for a reviewed ABI and feature set.")
endif ()
if (JUICYSF_RELEASE_VALIDATION)
  if (NOT JUICYSF_SF3_FIXTURE AND FLUIDSYNTH_PREFIX)
    set(_juicysf_installed_sf3
        "${FLUIDSYNTH_PREFIX}/share/juicy16-test-fixtures/VintageDreamsWaves-v2.sf3")
    if (EXISTS "${_juicysf_installed_sf3}")
      set(JUICYSF_SF3_FIXTURE "${_juicysf_installed_sf3}")
    endif ()
  endif ()
  if (NOT JUICYSF_SF3_FIXTURE OR NOT EXISTS "${JUICYSF_SF3_FIXTURE}")
    message(FATAL_ERROR
      "Beta 1 release validation requires JUICYSF_SF3_FIXTURE to name a licensed local .sf3 file")
  endif ()
  # Signing input: an identity other than ad-hoc must actually resolve, or the
  # build silently produces an unsigned or differently signed artifact.
  if (APPLE AND JUICYSF_CODE_SIGN_IDENTITY AND
      NOT JUICYSF_CODE_SIGN_IDENTITY STREQUAL "-")
    execute_process(
      COMMAND /usr/bin/security find-identity -v -p codesigning
      OUTPUT_VARIABLE _juicysf_identities
      ERROR_QUIET)
    if (NOT _juicysf_identities MATCHES "${JUICYSF_CODE_SIGN_IDENTITY}")
      message(FATAL_ERROR
        "JUICYSF_CODE_SIGN_IDENTITY='${JUICYSF_CODE_SIGN_IDENTITY}' matches no "
        "codesigning identity in the keychain. Use '-' for an ad-hoc local "
        "build, or install the approved Developer ID identity.")
    endif ()
  endif ()

  get_filename_component(_juicysf_sf3_extension "${JUICYSF_SF3_FIXTURE}" EXT)
  string(TOLOWER "${_juicysf_sf3_extension}" _juicysf_sf3_extension)
  if (NOT _juicysf_sf3_extension STREQUAL ".sf3")
    message(FATAL_ERROR
      "JUICYSF_SF3_FIXTURE must have an .sf3 extension: ${JUICYSF_SF3_FIXTURE}")
  endif ()
endif ()

# CMAKE_OSX_DEPLOYMENT_TARGET controls objects compiled in this build, but it
# cannot make prebuilt static archives compatible with an older OS. Homebrew
# bottles, for example, may have been built for the developer's current macOS.
# Refuse those archives in strict validation instead of producing a binary that
# merely advertises an older LC_BUILD_VERSION.
if (JUICYSF_RELEASE_VALIDATION AND APPLE AND FLUIDSYNTH_LINK_STATIC)
  foreach (_dependency IN LISTS FLUIDSYNTH_STATIC_LINK_LIBRARIES)
    if (IS_ABSOLUTE "${_dependency}" AND _dependency MATCHES "\\.a$")
      execute_process(
        COMMAND /usr/bin/lipo -archs "${_dependency}"
        RESULT_VARIABLE _lipo_result
        OUTPUT_VARIABLE _dependency_architectures
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
      if (NOT _lipo_result EQUAL 0
          OR NOT _dependency_architectures MATCHES "(^| )arm64( |$)")
        message(FATAL_ERROR
          "Beta 1 dependency lacks arm64 support: ${_dependency}")
      endif ()

      execute_process(
        COMMAND /usr/bin/otool -l "${_dependency}"
        RESULT_VARIABLE _otool_result
        OUTPUT_VARIABLE _dependency_load_commands
        ERROR_QUIET)
      if (NOT _otool_result EQUAL 0)
        message(FATAL_ERROR
          "Could not inspect the deployment target of ${_dependency}")
      endif ()
      string(REGEX MATCHALL "minos[ \t]+[0-9]+(\\.[0-9]+)*"
             _dependency_minos_lines "${_dependency_load_commands}")
      if (NOT _dependency_minos_lines)
        message(FATAL_ERROR
          "Dependency has no inspectable macOS deployment target: ${_dependency}")
      endif ()
      foreach (_minos_line IN LISTS _dependency_minos_lines)
        string(REGEX REPLACE "minos[ \t]+" "" _dependency_minos "${_minos_line}")
        if (_dependency_minos VERSION_GREATER CMAKE_OSX_DEPLOYMENT_TARGET)
          message(FATAL_ERROR
            "Dependency ${_dependency} targets macOS ${_dependency_minos}, "
            "newer than the Beta 1 target ${CMAKE_OSX_DEPLOYMENT_TARGET}. "
            "Rebuild the dependency for macOS ${CMAKE_OSX_DEPLOYMENT_TARGET} or earlier.")
        endif ()
      endforeach ()
    endif ()
  endforeach ()
endif ()

if (JUICYSF_FLUIDSYNTH_CMAKE_CONFIG)
  # FluidSynth's own package config already carries the transitive static
  # closure through its imported target's INTERFACE_LINK_LIBRARIES.
  set(FLUIDSYNTH_TARGET FluidSynth::libfluidsynth)
elseif (FLUIDSYNTH_LINK_STATIC)
  # FindPkgConfig creates only PkgConfig::FLUIDSYNTH; its `--static` closure is
  # exposed through FLUIDSYNTH_STATIC_* variables. Wrap those in a real target so
  # Release builds include FluidSynth and every required transitive dependency.
  add_library(JuicySFFluidSynthStatic INTERFACE)
  target_include_directories(JuicySFFluidSynthStatic INTERFACE
    ${FLUIDSYNTH_STATIC_INCLUDE_DIRS})
  target_link_libraries(JuicySFFluidSynthStatic INTERFACE
    ${FLUIDSYNTH_STATIC_LINK_LIBRARIES})
  set(FLUIDSYNTH_TARGET JuicySFFluidSynthStatic)
else ()
  set(FLUIDSYNTH_TARGET PkgConfig::FLUIDSYNTH)
endif ()

# Scaffolding for a second macOS architecture, kept for a possible later
# universal build. Intel macOS is outside Beta 1 scope, so a release candidate
# must not be built through it: the result would be a fat binary while the
# configuration claims arm64.
if (EXTRA_ARCH_PKG_CONFIG_PATH AND JUICYSF_RELEASE_VALIDATION)
  message(FATAL_ERROR
    "EXTRA_ARCH_PKG_CONFIG_PATH builds a second architecture, which is outside "
    "the approved Beta 1 matrix (macOS arm64 only). Unset it for a release "
    "candidate, or revise the support matrix first.")
endif ()

if (EXTRA_ARCH_PKG_CONFIG_PATH AND JUICYSF_FLUIDSYNTH_CMAKE_CONFIG)
  message(FATAL_ERROR
    "EXTRA_ARCH_PKG_CONFIG_PATH is a pkg-config mechanism and cannot be combined "
    "with -DJUICYSF_FLUIDSYNTH_CMAKE_CONFIG=ON.")
endif ()

if (EXTRA_ARCH_PKG_CONFIG_PATH)
  set(ENV{PKG_CONFIG_PATH} "${EXTRA_ARCH_PKG_CONFIG_PATH}")
  pkg_search_module(EXTRA_ARCH_FLUIDSYNTH REQUIRED IMPORTED_TARGET fluidsynth>=2)
  if (FLUIDSYNTH_LINK_STATIC)
    add_library(JuicySFExtraArchFluidSynthStatic INTERFACE)
    target_include_directories(JuicySFExtraArchFluidSynthStatic INTERFACE
      ${EXTRA_ARCH_FLUIDSYNTH_STATIC_INCLUDE_DIRS})
    target_link_libraries(JuicySFExtraArchFluidSynthStatic INTERFACE
      ${EXTRA_ARCH_FLUIDSYNTH_STATIC_LINK_LIBRARIES})
    set(EXTRA_ARCH_FLUIDSYNTH_TARGET JuicySFExtraArchFluidSynthStatic)
  else ()
    set(EXTRA_ARCH_FLUIDSYNTH_TARGET PkgConfig::EXTRA_ARCH_FLUIDSYNTH)
  endif ()
endif (EXTRA_ARCH_PKG_CONFIG_PATH)

# A statically linked Windows closure is built with the static C runtime so the
# VST3 needs no Visual C++ redistributable on a tester's machine. The plugin must
# use the same runtime or the link fails, so this is derived rather than a
# separate knob. CMP0091 is NEW at this project's cmake_minimum_required.
if (MSVC AND FLUIDSYNTH_LINK_STATIC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif ()

set(VST3_COPY_DIR "${JUCE_VST3_COPY_DIR}" CACHE STRING "VST3 installation dir")
set(AU_COPY_DIR "${JUCE_AU_COPY_DIR}" CACHE STRING "AU installation dir")

set(JUICYSF_PLUGIN_FORMATS VST3 Standalone)
if (APPLE)
  list(PREPEND JUICYSF_PLUGIN_FORMATS AU)
endif ()

if (VST3_SDK_PATH)
  juce_set_vst3_sdk_path(${VST3_SDK_PATH})
endif (VST3_SDK_PATH)

# juce_set_aax_sdk_path(...)

# `juce_add_plugin` adds a static library target with the name passed as the first argument
# (AudioPluginExample here). This target is a normal CMake target, but has a lot of extra properties set
# up by default. As well as this shared code static library, this function adds targets for each of
# the formats specified by the FORMATS arguments. This function accepts many optional arguments.
# Check the readme at `docs/CMake API.md` in the JUCE repo for the full list.

# https://github.com/juce-framework/JUCE/blob/master/docs/CMake%20API.md
juce_add_plugin(JuicySFPlugin
    # VERSION ...                               # Set this if the plugin version is different to the project version
    ICON_BIG resources/Logo512.png                              # ICON_* arguments specify a path to an image file to use as an icon for the Standalone
    # ICON_SMALL ...
    COMPANY_NAME Pokestir                           # Public project/brand name
    COMPANY_WEBSITE "https://pokestir.com"
    COMPANY_EMAIL "contact@pokestir.com"
    COMPANY_COPYRIGHT "Copyright (c) 2026 Pokestir"
    BUNDLE_ID "com.pokestir.juicy16"
    DESCRIPTION "16-channel multitimbral DLS/SoundFont player"
    IS_SYNTH TRUE                       # Is this a synth or an effect?
    NEEDS_MIDI_INPUT TRUE               # Does the plugin need midi input?
    NEEDS_MIDI_OUTPUT FALSE              # Does the plugin need midi output?
    IS_MIDI_EFFECT FALSE                 # Is this plugin a MIDI effect?
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE    # Does the editor need keyboard focus?
    COPY_PLUGIN_AFTER_BUILD ${JUICYSF_COPY_PLUGIN_AFTER_BUILD}
    APP_SANDBOX_ENABLED TRUE
    APP_SANDBOX_OPTIONS com.apple.security.files.user-selected.read-only
    HARDENED_RUNTIME_ENABLED TRUE
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    AU_SANDBOX_SAFE TRUE
    PLUGIN_MANUFACTURER_CODE Pkst               # Frozen starting with Beta 1
    PLUGIN_CODE Jc16                            # Frozen starting with Beta 1
                                                # GarageBand 10.3 requires the first letter to be upper-case, and the remaining letters to be lower-case
    # AU is the primary macOS format (delivers per-channel MIDI program change
    # natively). VST3 is built for Cubase and Windows: JUCE converts host-delivered
    # VST3 program-change events (kCtrlProgramChange legacy events) to per-channel
    # MIDI in processBlock, and per-channel program PARAMETERS (progCh1..16, grouped
    # into chUnit1..16) provide instrument selection in hosts that don't deliver PC
    # events; the pinned wrapper's IUnitInfo maps MIDI channels to units
    # HALion-style. VST2 is intentionally not configurable in the Beta 1 build.
    FORMATS ${JUICYSF_PLUGIN_FORMATS}
    VST3_COPY_DIR "${VST3_COPY_DIR}"
    AU_COPY_DIR "${AU_COPY_DIR}"
    PRODUCT_NAME "Juicy16")        # The name of the final executable, which can differ from the target name

# JUCE attaches ad-hoc signing as a link POST_BUILD step, while its generated
# AppIcon copy is an independent output of the format target. On a parallel clean
# build the linker can therefore sign the bundle before AppIcon.icns exists,
# immediately invalidating the signature when the icon is copied afterwards.
# Make the generated icon an explicit prerequisite and copy it during PRE_LINK so
# every later signing step sees the bundle's final resources.
if (APPLE)
  set(JUICYSF_GENERATED_ICON
      "${CMAKE_CURRENT_BINARY_DIR}/JuicySFPlugin_artefacts/JuceLibraryCode/AppIcon.icns")
  add_custom_target(JuicySFPlugin_AppIconReady DEPENDS "${JUICYSF_GENERATED_ICON}")

  set(_juicysf_apple_formats
      "JuicySFPlugin_Standalone|Standalone|app"
      "JuicySFPlugin_AU|AU|component"
      "JuicySFPlugin_VST3|VST3|vst3")
  foreach (_entry IN LISTS _juicysf_apple_formats)
    string(REPLACE "|" ";" _parts "${_entry}")
    list(GET _parts 0 _format_target)
    list(GET _parts 1 _format_directory)
    list(GET _parts 2 _bundle_suffix)
    if (TARGET ${_format_target})
      set(_resource_directory
          "${CMAKE_CURRENT_BINARY_DIR}/JuicySFPlugin_artefacts/$<CONFIG>/${_format_directory}/Juicy16.${_bundle_suffix}/Contents/Resources")
      add_dependencies(${_format_target} JuicySFPlugin_AppIconReady)
      add_custom_command(TARGET ${_format_target} PRE_LINK
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_resource_directory}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${JUICYSF_GENERATED_ICON}" "${_resource_directory}/AppIcon.icns"
        VERBATIM)
      if (JUICYSF_CODE_SIGN_IDENTITY)
        # Clang's default linker signature covers only the Mach-O and leaves the
        # bundle's Info.plist/resources unsealed. Re-sign the completed bundle;
        # for VST3 this command is appended after JUCE creates moduleinfo.json.
        add_custom_command(TARGET ${_format_target} POST_BUILD
          COMMAND /usr/bin/codesign --force --deep
                  --sign "${JUICYSF_CODE_SIGN_IDENTITY}"
                  "$<TARGET_BUNDLE_DIR:${_format_target}>"
          VERBATIM)
      endif ()
    endif ()
  endforeach ()
endif ()

# https://cmake.org/cmake/help/latest/guide/tutorial/Selecting%20Static%20or%20Shared%20Libraries.html
set_target_properties(JuicySFPlugin PROPERTIES POSITION_INDEPENDENT_CODE ON)

# `juce_generate_juce_header` will create a JuceHeader.h for a given target, which will be generated
# into your build tree. This should be included with `#include <JuceHeader.h>`. The include path for
# this header will be automatically added to the target. The main function of the JuceHeader is to
# include all your JUCE module headers; if you're happy to include module headers directly, you
# probably don't need to call this.

# juce_generate_juce_header(JuicySFPlugin)

# `target_sources` adds source files to a target. We pass the target that needs the sources as the
# first argument, then a visibility parameter for the sources which should normally be PRIVATE.
# Finally, we supply a list of source files that will be built into the target. This is a standard
# CMake command.

add_subdirectory(Source)

# JUCE modules and the bundled Steinberg VST3 SDK compile into the same targets,
# so only our own translation units are held to the stricter policy.
juicysf_treat_warnings_as_errors(${JUICYSF_FIRST_PARTY_SOURCES})

# VST3 wrapper patch: the vendored JUCE 8.0.14 wrapper supplies the early static
# 17-unit/program-list structure Cubase queries before component connection,
# per-channel Program Change MIDI mapping with bounds checks, and the
# kIsProgramChange/kIsList flags on progCh1..16. It also preserves every
# program-parameter queue point as a timestamped channel Program Change rather
# than collapsing the queue at block start. Stock JUCE has no complete hook for
# these controller-side behaviors. Only this build's VST3 format target is
# swapped; the installed JUCE package and AU/Standalone targets are untouched.
if (TARGET juce_audio_plugin_client_VST3)
  set(_juicysf_stock_vst3_cpp
      "${JUCE_MODULES_DIR}/juce_audio_plugin_client/juce_audio_plugin_client_VST3.cpp")
  set(_juicysf_stock_vst3_mm
      "${JUCE_MODULES_DIR}/juce_audio_plugin_client/juce_audio_plugin_client_VST3.mm")
  file(SHA256 "${_juicysf_stock_vst3_cpp}" _juicysf_stock_vst3_cpp_sha256)
  file(SHA256 "${_juicysf_stock_vst3_mm}" _juicysf_stock_vst3_mm_sha256)
  if (NOT _juicysf_stock_vst3_cpp_sha256 STREQUAL
          "ae1186c98c011c8ccd3b17d1cc4e6c5ea67d5cebb443f08c56ae957fcd2e10d8"
      OR NOT _juicysf_stock_vst3_mm_sha256 STREQUAL
          "44cfaf16c5f843acd0c7efdac5ceb318b07e0bc6da65cc9829252af61517f0ac")
    message(FATAL_ERROR
      "JUCE 8.0.14 VST3 wrapper content differs from the patch base. "
      "Rebase and revalidate vendor/juce_patched before configuring.")
  endif ()
  set(_juicysf_patched_vst3_cpp
      "${CMAKE_SOURCE_DIR}/vendor/juce_patched/juce_audio_plugin_client_VST3.cpp")
  set(_juicysf_patched_vst3_mm
      "${CMAKE_SOURCE_DIR}/vendor/juce_patched/juce_audio_plugin_client_VST3.mm")
  set(_juicysf_vst3_patch
      "${CMAKE_SOURCE_DIR}/vendor/juce_patched/juce-8.0.14-vst3-multitimbral.patch")
  file(SHA256 "${_juicysf_patched_vst3_cpp}" _juicysf_patched_vst3_cpp_sha256)
  file(SHA256 "${_juicysf_patched_vst3_mm}" _juicysf_patched_vst3_mm_sha256)
  file(SHA256 "${_juicysf_vst3_patch}" _juicysf_vst3_patch_sha256)
  if (NOT _juicysf_patched_vst3_cpp_sha256 STREQUAL
          "60cd751e32be8487e7e3d5903ce9706b015892dfddf76e4b0407ceb2d5e0543e"
      OR NOT _juicysf_patched_vst3_mm_sha256 STREQUAL
          "f15ba7b2eaee6dab3cc96c1e242ec782ee8503a72d4d9d0bda0c7d3261f3d7d3"
      OR NOT _juicysf_vst3_patch_sha256 STREQUAL
          "6b17dc0975b0f640a79545725fabc218c484a316b2dc1eac9663b7785c0de388")
    message(FATAL_ERROR
      "Vendored JUCE VST3 wrapper or its reproducible patch changed without updating the compatibility contract. "
      "Recreate the diff, run vst3_multitimbral_smoke, and record the reviewed hashes.")
  endif ()
  get_target_property(_vst3_client_sources juce_audio_plugin_client_VST3 INTERFACE_SOURCES)
  # On Apple the module compiles the .mm shim (which quote-includes the .cpp next
  # to it — our vendored .mm therefore pulls our patched .cpp); elsewhere it
  # compiles the .cpp directly. Handle both.
  set(_vst3_client_new_sources "")
  set(_vst3_client_swapped FALSE)
  foreach (_src IN LISTS _vst3_client_sources)
    # sources may be wrapped in generator expressions, so match/replace the path
    # inside the string rather than the whole element
    if (_src MATCHES "juce_audio_plugin_client_VST3\\.(cpp|mm)")
      string(REGEX REPLACE "[^;<>$]*juce_audio_plugin_client_VST3\\.(cpp|mm)"
             "${CMAKE_SOURCE_DIR}/vendor/juce_patched/juce_audio_plugin_client_VST3.\\1"
             _src "${_src}")
      set(_vst3_client_swapped TRUE)
    endif ()
    list(APPEND _vst3_client_new_sources "${_src}")
  endforeach ()
  if (NOT _vst3_client_swapped)
    message(FATAL_ERROR "Could not find juce_audio_plugin_client_VST3.{cpp,mm} in the VST3 client target's sources — JUCE layout changed; update the vendored patch wiring. Sources were: ${_vst3_client_sources}")
  endif ()
  set_target_properties(juce_audio_plugin_client_VST3 PROPERTIES INTERFACE_SOURCES "${_vst3_client_new_sources}")
  message(STATUS "VST3 wrapper source swapped for vendored kIsProgramChange patch")
endif ()

# Source/VST3Multitimbral.cpp includes VST3 SDK interface headers (header-only —
# no SDK symbols are linked; see that file) from JUCE's bundled copy, to implement
# per-channel units/program lists. The bundled SDK lives inside the
# juce_audio_processors module; some installs name the module directory with a
# "_headless" suffix.
set(JUCE_VST3_SDK_DIR "${JUCE_MODULES_DIR}/juce_audio_processors_headless/format_types/VST3_SDK")
if (NOT EXISTS "${JUCE_VST3_SDK_DIR}")
  set(JUCE_VST3_SDK_DIR "${JUCE_MODULES_DIR}/juce_audio_processors/format_types/VST3_SDK")
endif ()
if (NOT EXISTS "${JUCE_VST3_SDK_DIR}")
  message(FATAL_ERROR "Cannot find JUCE's bundled VST3 SDK under ${JUCE_MODULES_DIR}")
endif ()
# SYSTEM: the bundled Steinberg headers are third party, so their warnings must
# not be attributed to (or fail) our own translation units.
target_include_directories(JuicySFPlugin SYSTEM PRIVATE "${JUCE_VST3_SDK_DIR}")
# Source/ on the include path so the vendored VST3 wrapper can #include "Vst3Diag.h"
# (JUCE re-exports this target's INCLUDE_DIRECTORIES to the format wrapper targets).
target_include_directories(JuicySFPlugin PRIVATE "${CMAKE_SOURCE_DIR}/Source")

# `target_compile_definitions` adds some preprocessor definitions to our target. In a Projucer
# project, these might be passed in the 'Preprocessor Definitions' field. JUCE modules also make use
# of compile definitions to switch certain features on/off, so if there's a particular feature you
# need that's not on by default, check the module header for the correct flag to set here. These
# definitions will be visible both to your code, and also the JUCE module code, so for new
# definitions, pick unique names that are unlikely to collide! This is a standard CMake command.

target_compile_definitions(JuicySFPlugin
    PUBLIC
        JUICY16_VERSION="${JUICYSF_DISPLAY_VERSION}"
        # JUCE_WEB_BROWSER and JUCE_USE_CURL would be on by default, but you might not need them.
        JUCE_WEB_BROWSER=0  # If you remove this, add `NEEDS_WEB_BROWSER TRUE` to the `juce_add_plugin` call
        JUCE_USE_CURL=0     # If you remove this, add `NEEDS_CURL TRUE` to the `juce_add_plugin` call
        # JUCE_CORE_INCLUDE_OBJC_HELPERS=1
        DONT_SET_USING_JUCE_NAMESPACE=1
        JUCE_VST3_CAN_REPLACE_VST2=0)

if (WIN32)
  # Microsoft maps all Windows 10 releases to WINVER/_WIN32_WINNT 0x0A00;
  # NTDDI_WIN10_RS1 (0x0A000002) supplies the approved 1607 feature floor.
  target_compile_definitions(JuicySFPlugin PUBLIC
    WINVER=0x0A00
    _WIN32_WINNT=0x0A00
    NTDDI_VERSION=0x0A000002)
endif ()

message(STATUS "Juicy16 ${JUICYSF_DISPLAY_VERSION}")
message(STATUS "  Formats: ${JUICYSF_PLUGIN_FORMATS} (Standalone is development/QA only)")
message(STATUS "  JUCE: ${JUCE_VERSION}")
message(STATUS "  FluidSynth: ${FLUIDSYNTH_VERSION} (${FLUIDSYNTH_TARGET})")
if (EXISTS "${JUICYSF_SYSTEM_DLS}")
  message(STATUS "  System DLS capability test: ${JUICYSF_SYSTEM_DLS}")
else ()
  message(STATUS "  System DLS capability test: no system DLS found; DLS is unproven here")
endif ()
if (JUICYSF_FLUIDSYNTH_CMAKE_CONFIG)
  message(STATUS "  FluidSynth found via: CMake package config at ${FluidSynth_DIR}")
else ()
  message(STATUS "  FluidSynth found via: pkg-config")
endif ()
if (MSVC)
  message(STATUS "  MSVC runtime: ${CMAKE_MSVC_RUNTIME_LIBRARY}")
endif ()
message(STATUS "  Copy after build: ${JUICYSF_COPY_PLUGIN_AFTER_BUILD}")
if (JUICYSF_FONT_CORPUS)
  message(STATUS "  Private/local font corpus: ${JUICYSF_FONT_CORPUS}")
else ()
  message(STATUS "  Private/local font corpus: disabled")
endif ()
if (APPLE)
  if (CMAKE_OSX_ARCHITECTURES)
    set(_juicysf_arch_summary "${CMAKE_OSX_ARCHITECTURES}")
  else ()
    set(_juicysf_arch_summary "native toolchain default (not release-approved)")
  endif ()
  if (CMAKE_OSX_DEPLOYMENT_TARGET)
    set(_juicysf_deployment_summary "${CMAKE_OSX_DEPLOYMENT_TARGET}")
  else ()
    set(_juicysf_deployment_summary "toolchain default (not release-approved)")
  endif ()
  message(STATUS "  macOS architectures: ${_juicysf_arch_summary}")
  message(STATUS "  macOS deployment target: ${_juicysf_deployment_summary}")
elseif (WIN32)
  message(STATUS "  Windows architecture: x86_64")
  message(STATUS "  Windows minimum: 10 version 1607 (RS1; NTDDI 0x0A000002)")
endif ()
message(STATUS "  Strict release validation: ${JUICYSF_RELEASE_VALIDATION}")

# If your target needs extra binary assets, you can add them here. The first argument is the name of
# a new static library target that will include all the binary resources. There is an optional
# `NAMESPACE` argument that can specify the namespace of the generated binary data class. Finally,
# the SOURCES argument should be followed by a list of source files that should be built into the
# static library. These source files can be of any kind (wav data, images, fonts, icons etc.).
# Conversion to binary-data will happen when your target is built.

# The owner's wordmark, compiled into the plugin so the header can draw it in
# every format without a bundle resource to find at runtime.
juce_add_binary_data(Juicy16Assets SOURCES resources/juicy16-logo.png)
# VST3 and AU are loadable modules, so everything linked into them must be
# position-independent; a static library is not by default.
set_target_properties(Juicy16Assets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

# `target_link_libraries` links libraries and JUCE modules to other libraries or executables. Here,
# we're linking our executable target to the `juce::juce_audio_utils` module. Inter-module
# dependencies are resolved automatically, so `juce_core`, `juce_events` and so on will also be
# linked automatically. If we'd generated a binary data target above, we would need to link to it
# here too. This is a standard CMake command.

target_link_libraries(JuicySFPlugin
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_plugin_client
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
        ${FLUIDSYNTH_TARGET}
        ${EXTRA_ARCH_FLUIDSYNTH_TARGET}
    PUBLIC
        # PUBLIC, not PRIVATE: juce_add_binary_data attaches both
        # JUCE_TARGET_HAS_BINARY_DATA and BinaryData.h's include directory as
        # INTERFACE usage requirements, and JuceHeader.h includes BinaryData.h
        # whenever that definition is set. The test and tool executables compile
        # the same headers, so they need both or neither.
        Juicy16Assets
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Retain static runtime flags only for the unsupported MinGW experiment. The
# approved Windows release toolchain is MSVC and must not receive GNU flags.
if (MINGW AND NOT BUILD_SHARED_LIBS)
  set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++ ${CMAKE_EXE_LINKER_FLAGS}")
  # VST3 is a module, so it does not use executable linker flags.
  set(CMAKE_MODULE_LINKER_FLAGS "-static-libgcc -static-libstdc++ ${CMAKE_MODULE_LINKER_FLAGS}")
endif ()

if (BUILD_TESTING)
  add_test(NAME documentation_internal_links
    COMMAND ${CMAKE_COMMAND}
      -DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/DocumentationLinkTests.cmake)
  set_tests_properties(documentation_internal_links PROPERTIES LABELS "docs")

  # The vendored libsndfile security patch is applied by the dependency recipes,
  # which CMake never runs. This only guards them against drifting apart.
  add_test(NAME dependency_patch_contract
    COMMAND ${CMAKE_COMMAND}
      -DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/DependencyPatchTests.cmake)
  set_tests_properties(dependency_patch_contract PROPERTIES LABELS "docs;security")

  add_executable(JuicySFFontQA tools/font_qa.cpp)
  juicysf_treat_warnings_as_errors(tools/font_qa.cpp)
  target_link_libraries(JuicySFFontQA PRIVATE ${FLUIDSYNTH_TARGET})
  if (APPLE AND FLUIDSYNTH_LINK_STATIC)
    # The Homebrew static FluidSynth archive references CoreMIDI directly.
    # Plugin targets already receive it transitively from JUCE, but this
    # standalone QA executable does not.
    target_link_libraries(JuicySFFontQA PRIVATE "-framework CoreMIDI")
  endif ()
  target_compile_features(JuicySFFontQA PRIVATE cxx_std_17)
  add_test(NAME font_repair_unit COMMAND JuicySFFontQA)
  if (JUICYSF_RELEASE_VALIDATION)
    add_test(NAME font_load_release_sf3
             COMMAND JuicySFFontQA "${JUICYSF_SF3_FIXTURE}")
    set_tests_properties(font_load_release_sf3 PROPERTIES
      LABELS "font;sf3;release")
  endif ()
  if (JUICYSF_FONT_CORPUS)
    if (NOT EXISTS "${JUICYSF_FONT_CORPUS}")
      message(FATAL_ERROR
        "JUICYSF_FONT_CORPUS does not exist: ${JUICYSF_FONT_CORPUS}")
    endif ()
    add_test(NAME font_load_configured_corpus
             COMMAND JuicySFFontQA "${JUICYSF_FONT_CORPUS}")
    set_tests_properties(font_load_configured_corpus PROPERTIES
      LABELS "font;corpus")
  endif ()

  # Both platforms ship a General MIDI DLS bank. Exercising it gives clean
  # checkouts a real parser/load test without adding a large copyrighted fixture
  # to the repo, and on Windows it is the only DLS capability proof available to
  # CI, where the private corpus cannot be present.
  if (EXISTS "${JUICYSF_SYSTEM_DLS}")
    add_test(NAME font_load_system_dls
             COMMAND JuicySFFontQA "${JUICYSF_SYSTEM_DLS}")
    set_tests_properties(font_load_system_dls PROPERTIES LABELS "font;dls")
    if (JUICYSF_RELEASE_VALIDATION)
      set_tests_properties(font_load_system_dls PROPERTIES LABELS "font;dls;release")
    endif ()
  endif ()
  if (WIN32 AND JUICYSF_RELEASE_VALIDATION)
    add_test(NAME font_load_release_dls
             COMMAND JuicySFFontQA "${JUICYSF_RELEASE_DLS_PROBE}")
    set_tests_properties(font_load_release_dls PROPERTIES LABELS "font;dls;release")
  endif ()

  if (APPLE)
    add_executable(JuicySFVST3Smoke tools/vst3_smoke.cpp)
    juicysf_treat_warnings_as_errors(tools/vst3_smoke.cpp)
    target_include_directories(JuicySFVST3Smoke SYSTEM PRIVATE "${JUCE_VST3_SDK_DIR}")
    target_link_libraries(JuicySFVST3Smoke PRIVATE "-framework CoreFoundation")
    target_compile_features(JuicySFVST3Smoke PRIVATE cxx_std_17)
    add_dependencies(JuicySFVST3Smoke JuicySFPlugin_VST3)
    set(_juicysf_vst3_smoke_arguments
        "${CMAKE_CURRENT_BINARY_DIR}/JuicySFPlugin_artefacts/$<CONFIG>/VST3/Juicy16.vst3")
    if (EXISTS "${JUICYSF_MACOS_SYSTEM_DLS}")
      list(APPEND _juicysf_vst3_smoke_arguments "${JUICYSF_MACOS_SYSTEM_DLS}")
      list(APPEND _juicysf_vst3_smoke_arguments
           "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/vst3_multichannel_programs.csv")
    endif ()
    add_test(NAME vst3_multitimbral_smoke
      COMMAND JuicySFVST3Smoke ${_juicysf_vst3_smoke_arguments})
    set_tests_properties(vst3_multitimbral_smoke PROPERTIES LABELS "vst3;host")

    # In-process AU host. Registers the built component from its own bundle with
    # AudioComponentRegister, so nothing is installed and an already-installed
    # copy can never be tested by mistake.
    add_executable(JuicySFAUSmoke tools/au_smoke.cpp)
    juicysf_treat_warnings_as_errors(tools/au_smoke.cpp)
    target_link_libraries(JuicySFAUSmoke PRIVATE
      "-framework AudioToolbox" "-framework CoreFoundation")
    target_compile_features(JuicySFAUSmoke PRIVATE cxx_std_17)
    add_dependencies(JuicySFAUSmoke JuicySFPlugin_AU)
    set(_juicysf_au_smoke_arguments
        "${CMAKE_CURRENT_BINARY_DIR}/JuicySFPlugin_artefacts/$<CONFIG>/AU/Juicy16.component")
    if (EXISTS "${JUICYSF_MACOS_SYSTEM_DLS}")
      list(APPEND _juicysf_au_smoke_arguments "${JUICYSF_MACOS_SYSTEM_DLS}")
    endif ()
    add_test(NAME au_host_smoke
      COMMAND JuicySFAUSmoke ${_juicysf_au_smoke_arguments})
    set_tests_properties(au_host_smoke PROPERTIES LABELS "au;host;macos")
    add_test(NAME release_metadata_consistency
      COMMAND ${CMAKE_COMMAND}
        -DPROJECT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DARTIFACTS_DIR=${CMAKE_CURRENT_BINARY_DIR}/JuicySFPlugin_artefacts/$<CONFIG>
        -DPROJECT_VERSION=${PROJECT_VERSION}
        -DDISPLAY_VERSION=${JUICYSF_DISPLAY_VERSION}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/MetadataTests.cmake)
    if (JUICYSF_RELEASE_VALIDATION)
      add_test(NAME macos_artifact_portability
        COMMAND ${CMAKE_COMMAND}
          -DARTIFACTS_DIR=${CMAKE_CURRENT_BINARY_DIR}/JuicySFPlugin_artefacts/$<CONFIG>
          -DEXPECTED_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/MacArtifactTests.cmake)
      set_tests_properties(macos_artifact_portability PROPERTIES
        LABELS "artifact;release;macos")
    endif ()
  endif ()

  add_executable(JuicySFEngineMidiTests tests/EngineMidiTests.cpp)
  juicysf_treat_warnings_as_errors(tests/EngineMidiTests.cpp)
  target_include_directories(JuicySFEngineMidiTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
  target_link_libraries(JuicySFEngineMidiTests PRIVATE
    JuicySFPlugin
    juce::juce_audio_utils
    juce::juce_gui_extra
    ${FLUIDSYNTH_TARGET})
  target_compile_features(JuicySFEngineMidiTests PRIVATE cxx_std_17)
  # Registered on any platform with a system DLS, Windows included. The suite
  # creates an editor, so it needs a desktop session; that is true of the machines
  # this runs on. A failure on Windows is new information rather than a
  # regression, since it has never run there.
  if (EXISTS "${JUICYSF_SYSTEM_DLS}")
    add_test(NAME engine_midi_system_dls
             COMMAND JuicySFEngineMidiTests
               "${JUICYSF_SYSTEM_DLS}"
               "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/controller_conformance.csv")
    set_tests_properties(engine_midi_system_dls PROPERTIES LABELS "engine")
  endif ()

  # Randomised MIDI soak. Fixed seed and block count so CI is deterministic and
  # fast; the seed is an argument so a developer can sweep far wider locally.
  # See docs/MIDI_SOAK.md.
  if (EXISTS "${JUICYSF_SYSTEM_DLS}")
    add_test(NAME engine_midi_soak
             COMMAND JuicySFEngineMidiTests --midi-soak "${JUICYSF_SYSTEM_DLS}" 2000 1)
    set_tests_properties(engine_midi_soak PROPERTIES LABELS "engine;fuzz")
  endif ()

  # The importable host-test fixtures, validated offline against the system DLS
  # before anyone carries them into a DAW. These are the same files
  # docs/HOST_TEST_PROTOCOL.md asks a tester to import, so a host that produces a
  # different result is the host's problem, not the fixture's. Unlike the game-rip
  # tests these need no private corpus, so they run on macOS and Windows alike.
  if (EXISTS "${JUICYSF_SYSTEM_DLS}")
    foreach (_juicysf_host_fixture IN ITEMS host_program_matrix host_controllers)
      add_test(NAME engine_${_juicysf_host_fixture}
               COMMAND JuicySFEngineMidiTests --game-rip
                 "${JUICYSF_SYSTEM_DLS}"
                 "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/host/${_juicysf_host_fixture}.mid")
      set_tests_properties(engine_${_juicysf_host_fixture} PROPERTIES
        LABELS "engine;hostfixture")
    endforeach ()
  endif ()

  # The committed fixtures must stay byte-identical to what the generator emits,
  # or the expected-value tables in the protocol document stop describing them.
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if (Python3_Interpreter_FOUND)
    add_test(NAME host_fixtures_reproducible
      COMMAND ${CMAKE_COMMAND}
        -DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
        -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/HostFixtureTests.cmake)
    set_tests_properties(host_fixtures_reproducible PROPERTIES LABELS "hostfixture")
  endif ()

  # Resource and throughput baseline. Registered against the macOS system DLS so
  # a clean clone gets a real measurement, and additionally per corpus bank.
  # Amplitude-domain diagnostic. Not a pass/fail gate: it prints velocity, CC7,
  # CC10, and CC71-79 response curves for the plugin next to a stock FluidSynth
  # loaded with the same bank, so a dynamics complaint can be attributed to the
  # plugin, the engine, or the bank instead of guessed at.
  add_executable(JuicySFDynamicsProbe tools/dynamics_probe.cpp)
  juicysf_treat_warnings_as_errors(tools/dynamics_probe.cpp)
  target_include_directories(JuicySFDynamicsProbe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
  target_link_libraries(JuicySFDynamicsProbe PRIVATE
    JuicySFPlugin
    juce::juce_audio_utils
    juce::juce_gui_extra
    ${FLUIDSYNTH_TARGET})
  target_compile_features(JuicySFDynamicsProbe PRIVATE cxx_std_17)

  add_executable(JuicySFPerfProbe tools/perf_probe.cpp)
  juicysf_treat_warnings_as_errors(tools/perf_probe.cpp)
  target_include_directories(JuicySFPerfProbe PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
  target_link_libraries(JuicySFPerfProbe PRIVATE
    JuicySFPlugin
    juce::juce_audio_utils
    juce::juce_gui_extra
    ${FLUIDSYNTH_TARGET})
  target_compile_features(JuicySFPerfProbe PRIVATE cxx_std_17)
  # Resident-memory measurement is macOS-only inside the probe, so on Windows this
  # still exercises load, render throughput, and the voice ceiling while reporting
  # 0 MB for the memory deltas. Registered anyway: the throughput and voice-ceiling
  # assertions are the ones that would catch a bad Windows build.
  if (EXISTS "${JUICYSF_SYSTEM_DLS}")
    add_test(NAME performance_baseline
             COMMAND JuicySFPerfProbe "${JUICYSF_SYSTEM_DLS}")
    set_tests_properties(performance_baseline PROPERTIES LABELS "performance")
  endif ()

  # Optional end-to-end proof of the non-negotiable workflow: play a real
  # multichannel rip and require every channel to reach the instrument its own
  # Program Change selected, with no manual patch assignment. Game rips are not
  # redistributable, so this runs only against a configured private corpus. A
  # bank sharing the MIDI file's basename is treated as its matching bank, and
  # every such bank format is registered separately.
  if (JUICYSF_FONT_CORPUS)
    file(GLOB_RECURSE JUICYSF_CORPUS_MIDI LIST_DIRECTORIES FALSE
      "${JUICYSF_FONT_CORPUS}/*.mid" "${JUICYSF_FONT_CORPUS}/*.MID")
    list(REMOVE_DUPLICATES JUICYSF_CORPUS_MIDI)
    set(_juicysf_game_rip_tests 0)
    foreach (_midi IN LISTS JUICYSF_CORPUS_MIDI)
      get_filename_component(_midi_dir "${_midi}" DIRECTORY)
      get_filename_component(_midi_stem "${_midi}" NAME_WE)
      # One test per bank format. The uppercase spelling is only consulted when
      # the lowercase one is absent, so a case-insensitive filesystem does not
      # register the same file twice.
      foreach (_extension IN ITEMS dls sf2 sf3)
        set(_bank "${_midi_dir}/${_midi_stem}.${_extension}")
        if (NOT EXISTS "${_bank}")
          string(TOUPPER "${_extension}" _upper_extension)
          set(_bank "${_midi_dir}/${_midi_stem}.${_upper_extension}")
        endif ()
        if (EXISTS "${_bank}")
          math(EXPR _juicysf_game_rip_tests "${_juicysf_game_rip_tests} + 1")
          add_test(NAME engine_game_rip_${_midi_stem}_${_extension}
                   COMMAND JuicySFEngineMidiTests --game-rip "${_bank}" "${_midi}")
          set_tests_properties(engine_game_rip_${_midi_stem}_${_extension}
            PROPERTIES LABELS "engine;corpus;gamerip")
        endif ()
      endforeach ()
    endforeach ()
    if (_juicysf_game_rip_tests EQUAL 0)
      message(STATUS
        "  Game-rip tests: none (no .mid with a same-named bank under the corpus)")
    else ()
      message(STATUS "  Game-rip tests: ${_juicysf_game_rip_tests}")
    endif ()
  endif ()
endif ()
