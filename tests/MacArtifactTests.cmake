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
  # The first line is the inspected executable path, not a linked dependency.
  string(REGEX REPLACE "^[^\n]*\n" "" OTOOL_DEPENDENCIES "${OTOOL_LIBS_OUTPUT}")
  if (OTOOL_DEPENDENCIES MATCHES
      "(/opt/homebrew|/usr/local|/Users/[A-Za-z0-9._-]+/|/private/tmp|/var/folders/)")
    message(FATAL_ERROR
      "Release artifact links a developer/build path: ${BINARY}\n${OTOOL_DEPENDENCIES}")
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
