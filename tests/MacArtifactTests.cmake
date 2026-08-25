if (NOT DEFINED ARTIFACTS_DIR OR NOT DEFINED EXPECTED_DEPLOYMENT_TARGET)
  message(FATAL_ERROR "macOS artifact test arguments are incomplete")
endif ()
if (NOT DEFINED INCLUDE_STANDALONE)
  set(INCLUDE_STANDALONE ON)
endif ()

set(AU_BUNDLE "${ARTIFACTS_DIR}/AU/Juicy16.component")
set(VST3_BUNDLE "${ARTIFACTS_DIR}/VST3/Juicy16.vst3")
set(STANDALONE_BUNDLE "${ARTIFACTS_DIR}/Standalone/Juicy16.app")

set(BUNDLES
    "${AU_BUNDLE}"
    "${VST3_BUNDLE}")
set(BINARIES
    "${AU_BUNDLE}/Contents/MacOS/Juicy16"
    "${VST3_BUNDLE}/Contents/MacOS/Juicy16")
if (INCLUDE_STANDALONE)
  list(APPEND BUNDLES "${STANDALONE_BUNDLE}")
  list(APPEND BINARIES "${STANDALONE_BUNDLE}/Contents/MacOS/Juicy16")
  set(FORMAT_SUMMARY "AU, VST3, and Standalone")
else ()
  set(FORMAT_SUMMARY "AU and VST3")
endif ()

foreach (BUNDLE IN LISTS BUNDLES)
  if (NOT EXISTS "${BUNDLE}")
    message(FATAL_ERROR
      "Expected release artifact is absent: ${BUNDLE}. Build AU, VST3, and Standalone before CTest.")
  endif ()

  execute_process(
    COMMAND /usr/bin/codesign --verify --deep --strict --verbose=2 "${BUNDLE}"
    RESULT_VARIABLE CODESIGN_RESULT
    ERROR_VARIABLE CODESIGN_ERROR)
  if (NOT CODESIGN_RESULT EQUAL 0)
    message(FATAL_ERROR
      "Strict code-signature verification failed for ${BUNDLE}:\n${CODESIGN_ERROR}")
  endif ()
endforeach ()

foreach (BINARY IN LISTS BINARIES)
  if (NOT EXISTS "${BINARY}")
    message(FATAL_ERROR "Expected release executable is absent: ${BINARY}")
  endif ()

  execute_process(
    COMMAND /usr/bin/file "${BINARY}"
    RESULT_VARIABLE FILE_RESULT
    OUTPUT_VARIABLE FILE_OUTPUT
    ERROR_VARIABLE FILE_ERROR)
  if (NOT FILE_RESULT EQUAL 0)
    message(FATAL_ERROR "Could not inspect ${BINARY}: ${FILE_ERROR}")
  endif ()
  if (NOT FILE_OUTPUT MATCHES "Mach-O 64-bit.*arm64" OR FILE_OUTPUT MATCHES "x86_64")
    message(FATAL_ERROR
      "Beta 1 macOS artifact is not arm64-only: ${FILE_OUTPUT}")
  endif ()

  execute_process(
    COMMAND /usr/bin/otool -L "${BINARY}"
    RESULT_VARIABLE OTOOL_LIBS_RESULT
    OUTPUT_VARIABLE OTOOL_LIBS_OUTPUT
    ERROR_VARIABLE OTOOL_LIBS_ERROR)
  if (NOT OTOOL_LIBS_RESULT EQUAL 0)
    message(FATAL_ERROR
      "Could not inspect linked dependencies for ${BINARY}: ${OTOOL_LIBS_ERROR}")
  endif ()
  # Dependency lines are the tab-indented ones; the first line is the inspected
  # binary's own path. Split on newlines rather than stripping the header with a
  # regex: `string(REGEX REPLACE "^[^\n]*\n" "" ...)` empties the whole string in
  # CMake, because a bracket expression there does not exclude a newline the way
  # it reads. That silently left this entire check inspecting an empty string,
  # so every artifact "passed" it without a single dependency being looked at.
  string(REPLACE "\n" ";" OTOOL_LINES "${OTOOL_LIBS_OUTPUT}")
  set(DEPENDENCY_COUNT 0)
  foreach (OTOOL_LINE IN LISTS OTOOL_LINES)
    if (NOT OTOOL_LINE MATCHES "^[ \t]+")
      continue()
    endif ()
    string(STRIP "${OTOOL_LINE}" DEPENDENCY_PATH)
    string(REGEX REPLACE " \\(compatibility version.*$" "" DEPENDENCY_PATH "${DEPENDENCY_PATH}")
    string(STRIP "${DEPENDENCY_PATH}" DEPENDENCY_PATH)
    if (DEPENDENCY_PATH STREQUAL "")
      continue()
    endif ()
    math(EXPR DEPENDENCY_COUNT "${DEPENDENCY_COUNT} + 1")

    if (DEPENDENCY_PATH MATCHES
        "(/opt/homebrew|/usr/local|/Users/[A-Za-z0-9._-]+/|/private/tmp|/var/folders/)")
      message(FATAL_ERROR
        "Release artifact links a developer/build path: ${BINARY}\n  ${DEPENDENCY_PATH}")
    endif ()

    # The product claim is that a tester installs the plugin and nothing else, so
    # every load command must resolve inside macOS itself. FluidSynth and every
    # codec are linked statically; anything outside /usr/lib or /System is a
    # dependency a user would have to go and install.
    if (NOT DEPENDENCY_PATH MATCHES "^(/usr/lib/|/System/)")
      message(FATAL_ERROR
        "Release artifact links a non-system library, which a user would have to "
        "install separately: ${BINARY}\n  ${DEPENDENCY_PATH}")
    endif ()
  endforeach ()

  # A parse that finds nothing must fail rather than pass: that is how the
  # previous version of this check went unnoticed.
  if (DEPENDENCY_COUNT EQUAL 0)
    message(FATAL_ERROR
      "Could not parse any linked dependency for ${BINARY}. Every Mach-O links at "
      "least libSystem, so this means the check is inspecting nothing.")
  endif ()

  execute_process(
    COMMAND /usr/bin/otool -l "${BINARY}"
    RESULT_VARIABLE OTOOL_LOAD_RESULT
    OUTPUT_VARIABLE OTOOL_LOAD_OUTPUT
    ERROR_VARIABLE OTOOL_LOAD_ERROR)
  if (NOT OTOOL_LOAD_RESULT EQUAL 0)
    message(FATAL_ERROR
      "Could not inspect deployment target for ${BINARY}: ${OTOOL_LOAD_ERROR}")
  endif ()
  string(REGEX MATCHALL "minos[ \t]+[0-9]+(\\.[0-9]+)*"
         MINOS_LINES "${OTOOL_LOAD_OUTPUT}")
  if (NOT MINOS_LINES)
    message(FATAL_ERROR "No macOS deployment target found in ${BINARY}")
  endif ()
  foreach (MINOS_LINE IN LISTS MINOS_LINES)
    string(REGEX REPLACE "minos[ \t]+" "" MINOS "${MINOS_LINE}")
    if (NOT MINOS VERSION_EQUAL EXPECTED_DEPLOYMENT_TARGET)
      message(FATAL_ERROR
        "${BINARY} targets macOS ${MINOS}, expected ${EXPECTED_DEPLOYMENT_TARGET}")
    endif ()
  endforeach ()

  execute_process(
    COMMAND /usr/bin/strings -a "${BINARY}"
    RESULT_VARIABLE STRINGS_RESULT
    OUTPUT_VARIABLE STRINGS_OUTPUT
    ERROR_VARIABLE STRINGS_ERROR)
  if (NOT STRINGS_RESULT EQUAL 0)
    message(FATAL_ERROR
      "Could not scan embedded strings in ${BINARY}: ${STRINGS_ERROR}")
  endif ()
  if (STRINGS_OUTPUT MATCHES
      "(/opt/homebrew|/usr/local|/Users/[A-Za-z0-9._-]+/|/private/tmp|/var/folders/)")
    message(FATAL_ERROR
      "Release artifact embeds a developer/build path: ${BINARY}")
  endif ()
endforeach ()

message(STATUS
  "${FORMAT_SUMMARY} are arm64-only, target macOS ${EXPECTED_DEPLOYMENT_TARGET}, pass strict signature checks, and contain no prohibited dependency or embedded paths")
