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

project(JUICY_SF_RACK VERSION 0.5.0)
set(JUICYSF_PRERELEASE_LABEL "beta.1" CACHE STRING
    "Optional prerelease label appended to the user-facing version")
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
set(JUICYSF_CODE_SIGN_IDENTITY "-" CACHE STRING
    "macOS code-sign identity used after bundle resources and VST3 metadata are finalized; empty disables signing")
set(VST2_SDK_PATH "" CACHE STRING "VST2 SDK path")
option(JUICYSF_ENABLE_LEGACY_VST2 "Build unsupported legacy VST2 when an SDK is supplied" off)

if (JUICYSF_RELEASE_VALIDATION)
  if (NOT FLUIDSYNTH_LINK_STATIC)
    message(FATAL_ERROR "Release validation requires FLUIDSYNTH_LINK_STATIC=ON")
  endif ()
  if (APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    message(FATAL_ERROR "Release validation requires an explicit CMAKE_OSX_DEPLOYMENT_TARGET")
  endif ()
  if (APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    message(FATAL_ERROR "Release validation requires explicit CMAKE_OSX_ARCHITECTURES")
  endif ()
endif ()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake/Modules/")
find_package(PkgConfig REQUIRED)

if (BUILD_SHARED_LIBS)
  set(TARGET_STATIC_QUALIFIER "")
else (BUILD_SHARED_LIBS)
  set(TARGET_STATIC_QUALIFIER "-static")
  set(PACKAGE_STATIC_QUALIFIER "_STATIC")
endif (BUILD_SHARED_LIBS)

set(PKG_CONFIG_USE_CMAKE_PREFIX_PATH off)
pkg_search_module(FLUIDSYNTH REQUIRED IMPORTED_TARGET fluidsynth>=2)
if (FLUIDSYNTH_LINK_STATIC)
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

set(VST_COPY_DIR "${JUCE_VST_COPY_DIR}" CACHE STRING "VST2 installation dir")
set(VST3_COPY_DIR "${JUCE_VST3_COPY_DIR}" CACHE STRING "VST3 installation dir")
set(AU_COPY_DIR "${JUCE_AU_COPY_DIR}" CACHE STRING "AU installation dir")

# If you are building a VST2 or AAX plugin, CMake needs to be told where to find these SDKs on your
# system. This setup should be done before calling `juce_add_plugin`.

# Legacy VST2 is excluded by default. When explicitly enabled, an in-repo SDK may
# be used, but VST2 remains outside the Beta 1 release contract.
if (JUICYSF_ENABLE_LEGACY_VST2 AND NOT VST2_SDK_PATH
    AND EXISTS "${CMAKE_SOURCE_DIR}/VST2_SDK/pluginterfaces/vst2.x/aeffect.h")
  set(VST2_SDK_PATH "${CMAKE_SOURCE_DIR}/VST2_SDK")
  message(STATUS "Found in-repo VST2 SDK at ${VST2_SDK_PATH}; building VST2 (.vst)")
endif ()

if (JUICYSF_ENABLE_LEGACY_VST2 AND VST2_SDK_PATH)
  set(HAS_VST2_SDK_PATH 1)
  set(VST2_FORMAT "VST")
  juce_set_vst2_sdk_path(${VST2_SDK_PATH})
else ()
  set(HAS_VST2_SDK_PATH 0)
endif ()

set(JUICYSF_PLUGIN_FORMATS VST3 Standalone)
if (APPLE)
  list(PREPEND JUICYSF_PLUGIN_FORMATS AU)
endif ()
if (VST2_FORMAT)
  list(APPEND JUICYSF_PLUGIN_FORMATS ${VST2_FORMAT})
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

# building of AUv3 requires -DCMAKE_GENERATOR=Xcode or -GXcode
# and your "XCode > Preferences > Locations > Command Line Tools" set to "XCode"
# otherwise you'll get error:
# Run Build Command(s):/usr/bin/xcodebuild -project CMAKE_TRY_COMPILE.xcodeproj build -target cmTC_33f26 -parallelizeTargets -configuration Debug -hideShellScriptEnvironment && xcode-select: error: tool 'xcodebuild' requires Xcode, but active developer directory '/Library/Developer/CommandLineTools' is a command line tools instance

# https://github.com/juce-framework/JUCE/blob/master/docs/CMake%20API.md
juce_add_plugin(JuicySFPlugin
    # VERSION ...                               # Set this if the plugin version is different to the project version
    ICON_BIG resources/Logo512.png                              # ICON_* arguments specify a path to an image file to use as an icon for the Standalone
    # ICON_SMALL ...
    COMPANY_NAME Birchlabs                          # Specify the name of the plugin's author
    COMPANY_WEBSITE "https://birchlabs.co.uk"
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
    VST2_CATEGORY kPlugCategSynth
    VST3_CATEGORIES Instrument Synth
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    AU_SANDBOX_SAFE TRUE
    PLUGIN_MANUFACTURER_CODE Blbs               # A four-character manufacturer id with at least one upper-case character
    PLUGIN_CODE Jsfr                            # A unique four-character plugin id with exactly one upper-case character
                                                # GarageBand 10.3 requires the first letter to be upper-case, and the remaining letters to be lower-case
    # AU is the primary macOS format (delivers per-channel MIDI program change
    # natively). VST3 is built for Cubase and Windows: JUCE converts host-delivered
    # VST3 program-change events (kCtrlProgramChange legacy events) to per-channel
    # MIDI in processBlock, and per-channel program PARAMETERS (progCh1..16, grouped
    # into chUnit1..16) provide instrument selection in hosts that don't deliver PC
    # events; a custom IUnitInfo (VST3ClientExtensions) maps MIDI channels to units
    # HALion-style. VST2 (.vst) only builds when VST2_SDK_PATH is set (see top of
    # file); otherwise ${VST2_FORMAT} is empty.
    FORMATS ${JUICYSF_PLUGIN_FORMATS}
    VST_COPY_DIR "${VST_COPY_DIR}"
    VST3_COPY_DIR "${VST3_COPY_DIR}"
    AU_COPY_DIR "${AU_COPY_DIR}"
    PRODUCT_NAME "JuicySF Rack")        # The name of the final executable, which can differ from the target name

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
          "${CMAKE_CURRENT_BINARY_DIR}/JuicySFPlugin_artefacts/$<CONFIG>/${_format_directory}/JuicySF Rack.${_bundle_suffix}/Contents/Resources")
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
set_target_properties(JuicySFPlugin PROPERTIES
                        POSITION_INDEPENDENT_CODE ${BUILD_SHARED_LIBS})

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

# VST3 wrapper patch: the vendored JUCE 8.0.14 wrapper supplies the early static
# 17-unit/program-list structure Cubase queries before component connection,
# per-channel Program Change MIDI mapping with bounds checks, and the
# kIsProgramChange/kIsList flags on progCh1..16. Stock JUCE has no complete hook
# for these controller-side behaviors. Only this build's VST3 format target is
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
target_include_directories(JuicySFPlugin PRIVATE "${JUCE_VST3_SDK_DIR}")
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
        JUICYSF_RACK_VERSION="${JUICYSF_DISPLAY_VERSION}"
        # JUCE_WEB_BROWSER and JUCE_USE_CURL would be on by default, but you might not need them.
        JUCE_WEB_BROWSER=0  # If you remove this, add `NEEDS_WEB_BROWSER TRUE` to the `juce_add_plugin` call
        JUCE_USE_CURL=0     # If you remove this, add `NEEDS_CURL TRUE` to the `juce_add_plugin` call
        # JUCE_CORE_INCLUDE_OBJC_HELPERS=1
        DONT_SET_USING_JUCE_NAMESPACE=1
        JUCE_VST3_CAN_REPLACE_VST2=${HAS_VST2_SDK_PATH})

message(STATUS "JuicySF Rack ${JUICYSF_DISPLAY_VERSION}")
message(STATUS "  Formats: ${JUICYSF_PLUGIN_FORMATS} (Standalone is development/QA only)")
message(STATUS "  JUCE: ${JUCE_VERSION}")
message(STATUS "  FluidSynth: ${FLUIDSYNTH_VERSION} (${FLUIDSYNTH_TARGET})")
message(STATUS "  Copy after build: ${JUICYSF_COPY_PLUGIN_AFTER_BUILD}")
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
endif ()
message(STATUS "  Strict release validation: ${JUICYSF_RELEASE_VALIDATION}")

# If your target needs extra binary assets, you can add them here. The first argument is the name of
# a new static library target that will include all the binary resources. There is an optional
# `NAMESPACE` argument that can specify the namespace of the generated binary data class. Finally,
# the SOURCES argument should be followed by a list of source files that should be built into the
# static library. These source files can be of any kind (wav data, images, fonts, icons etc.).
# Conversion to binary-data will happen when your target is built.

# juce_add_binary_data(AudioPluginData SOURCES ...)

# `target_link_libraries` links libraries and JUCE modules to other libraries or executables. Here,
# we're linking our executable target to the `juce::juce_audio_utils` module. Inter-module
# dependencies are resolved automatically, so `juce_core`, `juce_events` and so on will also be
# linked automatically. If we'd generated a binary data target above, we would need to link to it
# here too. This is a standard CMake command.

target_link_libraries(JuicySFPlugin
    PRIVATE
        # AudioPluginData           # If we'd created a binary data target, we'd link to it here
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
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Apple clang doesn't support -static-libgcc flag, but seems to make a portable binary regardless
if (NOT BUILD_SHARED_LIBS AND CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++ ${CMAKE_EXE_LINKER_FLAGS}")
  # the VST2 and VST3 distributions are "modules", so they don't listen to the EXE_LINKER flags
  set(CMAKE_MODULE_LINKER_FLAGS "-static-libgcc -static-libstdc++ ${CMAKE_MODULE_LINKER_FLAGS}")
endif (NOT BUILD_SHARED_LIBS AND CMAKE_SYSTEM_NAME STREQUAL "Windows")

include(CTest)
if (BUILD_TESTING)
  add_executable(JuicySFFontQA tools/font_qa.cpp)
  target_link_libraries(JuicySFFontQA PRIVATE ${FLUIDSYNTH_TARGET})
  if (APPLE AND FLUIDSYNTH_LINK_STATIC)
    # The Homebrew static FluidSynth archive references CoreMIDI directly.
    # Plugin targets already receive it transitively from JUCE, but this
    # standalone QA executable does not.
    target_link_libraries(JuicySFFontQA PRIVATE "-framework CoreMIDI")
  endif ()
  target_compile_features(JuicySFFontQA PRIVATE cxx_std_17)
  add_test(NAME font_repair_unit COMMAND JuicySFFontQA)

  # macOS ships a General MIDI DLS bank. Exercising it gives clean checkouts a
  # real parser/load test without adding a large copyrighted fixture to the repo.
  set(JUICYSF_MACOS_SYSTEM_DLS
      "/System/Library/Components/CoreAudio.component/Contents/Resources/gs_instruments.dls")
  if (APPLE AND EXISTS "${JUICYSF_MACOS_SYSTEM_DLS}")
    add_test(NAME font_load_system_dls
             COMMAND JuicySFFontQA "${JUICYSF_MACOS_SYSTEM_DLS}")
  endif ()

  if (APPLE)
    add_executable(JuicySFVST3Smoke tools/vst3_smoke.cpp)
    target_include_directories(JuicySFVST3Smoke PRIVATE "${JUCE_VST3_SDK_DIR}")
    target_link_libraries(JuicySFVST3Smoke PRIVATE "-framework CoreFoundation")
    target_compile_features(JuicySFVST3Smoke PRIVATE cxx_std_17)
    add_dependencies(JuicySFVST3Smoke JuicySFPlugin_VST3)
    add_test(NAME vst3_multitimbral_smoke
      COMMAND JuicySFVST3Smoke
              "${CMAKE_CURRENT_BINARY_DIR}/JuicySFPlugin_artefacts/$<CONFIG>/VST3/JuicySF Rack.vst3")
    add_test(NAME release_metadata_consistency
      COMMAND ${CMAKE_COMMAND}
        -DPROJECT_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DARTIFACTS_DIR=${CMAKE_CURRENT_BINARY_DIR}/JuicySFPlugin_artefacts/$<CONFIG>
        -DPROJECT_VERSION=${PROJECT_VERSION}
        -DDISPLAY_VERSION=${JUICYSF_DISPLAY_VERSION}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/MetadataTests.cmake)
  endif ()

  add_executable(JuicySFEngineMidiTests tests/EngineMidiTests.cpp)
  target_include_directories(JuicySFEngineMidiTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Source")
  target_link_libraries(JuicySFEngineMidiTests PRIVATE
    JuicySFPlugin
    juce::juce_audio_utils
    juce::juce_gui_extra
    ${FLUIDSYNTH_TARGET})
  target_compile_features(JuicySFEngineMidiTests PRIVATE cxx_std_17)
  if (APPLE AND EXISTS "${JUICYSF_MACOS_SYSTEM_DLS}")
    add_test(NAME engine_midi_system_dls
             COMMAND JuicySFEngineMidiTests "${JUICYSF_MACOS_SYSTEM_DLS}")
  endif ()
endif ()
