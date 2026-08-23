if (NOT DEFINED PROJECT_SOURCE_DIR OR NOT DEFINED ARTIFACTS_DIR
    OR NOT DEFINED PROJECT_VERSION OR NOT DEFINED DISPLAY_VERSION)
  message(FATAL_ERROR "Metadata test arguments are incomplete")
endif ()

set(AU_PLIST
    "${ARTIFACTS_DIR}/AU/Juicy16.component/Contents/Info.plist")
set(VST3_INFO
    "${ARTIFACTS_DIR}/VST3/Juicy16.vst3/Contents/Resources/moduleinfo.json")
if (NOT EXISTS "${AU_PLIST}" OR NOT EXISTS "${VST3_INFO}")
  message(FATAL_ERROR "Expected AU/VST3 metadata is absent; build all release formats before CTest")
endif ()

execute_process(
  COMMAND /usr/bin/plutil -extract CFBundleShortVersionString raw "${AU_PLIST}"
  RESULT_VARIABLE PLUTIL_RESULT
  OUTPUT_VARIABLE AU_VERSION
  OUTPUT_STRIP_TRAILING_WHITESPACE)
if (NOT PLUTIL_RESULT EQUAL 0 OR NOT AU_VERSION STREQUAL PROJECT_VERSION)
  message(FATAL_ERROR "AU version '${AU_VERSION}' does not match '${PROJECT_VERSION}'")
endif ()

# DISPLAY_VERSION was previously only reported. It is the version a user reads in
# the status bar and the one the packager writes into the archive name and
# BUILD_INFO.txt, so it has to be checked against the binary that actually ships:
# a packager that derives it from a drifted CMake line must fail here.
set(AU_BINARY "${ARTIFACTS_DIR}/AU/Juicy16.component/Contents/MacOS/Juicy16")
if (NOT EXISTS "${AU_BINARY}")
  message(FATAL_ERROR "AU executable is absent: ${AU_BINARY}")
endif ()
execute_process(
  COMMAND /usr/bin/strings "${AU_BINARY}"
  RESULT_VARIABLE STRINGS_RESULT
  OUTPUT_VARIABLE AU_STRINGS)
if (NOT STRINGS_RESULT EQUAL 0)
  message(FATAL_ERROR "Could not read strings from ${AU_BINARY}")
endif ()
# Anchored on a digit so the status label's accessibility description
# ("Juicy16 version and latest ...") cannot be mistaken for the version itself.
string(REGEX MATCH "Juicy16 v[0-9][^ \n]*" AU_UI_VERSION "${AU_STRINGS}")
string(REPLACE "Juicy16 v" "" AU_UI_VERSION "${AU_UI_VERSION}")
if (NOT AU_UI_VERSION STREQUAL DISPLAY_VERSION)
  message(FATAL_ERROR
    "AU displays version '${AU_UI_VERSION}' but the caller expected '${DISPLAY_VERSION}'")
endif ()

function(assert_plist_value KEY EXPECTED_VALUE)
  execute_process(
    COMMAND /usr/bin/plutil -extract "${KEY}" raw "${AU_PLIST}"
    RESULT_VARIABLE PLIST_RESULT
    OUTPUT_VARIABLE PLIST_VALUE
    OUTPUT_STRIP_TRAILING_WHITESPACE)
  if (NOT PLIST_RESULT EQUAL 0 OR NOT PLIST_VALUE STREQUAL EXPECTED_VALUE)
    message(FATAL_ERROR
      "AU plist ${KEY} is '${PLIST_VALUE}', expected '${EXPECTED_VALUE}'")
  endif ()
endfunction()

assert_plist_value("CFBundleDisplayName" "Juicy16")
assert_plist_value("CFBundleExecutable" "Juicy16")
assert_plist_value("CFBundleIdentifier" "com.pokestir.juicy16")
assert_plist_value("NSHumanReadableCopyright" "Copyright (c) 2026 Pokestir")
assert_plist_value("AudioComponents.0.manufacturer" "Pkst")
assert_plist_value("AudioComponents.0.subtype" "Jc16")
assert_plist_value("AudioComponents.0.name" "Pokestir: Juicy16")

file(READ "${VST3_INFO}" VST3_METADATA)
string(REGEX MATCH "\"Version\"[ \t]*:[ \t]*\"${PROJECT_VERSION}\"" VST3_VERSION_MATCH
             "${VST3_METADATA}")
if (NOT VST3_VERSION_MATCH)
  message(FATAL_ERROR "VST3 moduleinfo does not contain version '${PROJECT_VERSION}'")
endif ()

foreach (EXPECTED_VST3_METADATA
    "\"Name\": \"Juicy16\""
    "\"Vendor\": \"Pokestir\""
    "\"URL\": \"https://pokestir.com\""
    "\"E-Mail\": \"contact@pokestir.com\""
    "\"CID\": \"ABCDEF019182FAEB506B73744A633136\""
    "\"CID\": \"ABCDEF011234ABCD506B73744A633136\"")
  string(FIND "${VST3_METADATA}" "${EXPECTED_VST3_METADATA}" VST3_METADATA_MATCH)
  if (VST3_METADATA_MATCH EQUAL -1)
    message(FATAL_ERROR
      "Approved Beta 1 identity missing from VST3 metadata: ${EXPECTED_VST3_METADATA}")
  endif ()
endforeach ()

file(READ "${PROJECT_SOURCE_DIR}/CMakeLists.txt" ROOT_CMAKE)
string(FIND "${ROOT_CMAKE}" "JUICY16_VERSION=\"\${JUICYSF_DISPLAY_VERSION}\"" DISPLAY_WIRING)
if (DISPLAY_WIRING EQUAL -1)
  message(FATAL_ERROR "UI display version is no longer wired to JUICYSF_DISPLAY_VERSION")
endif ()

foreach (EXPECTED_IDENTITY
    "COMPANY_NAME Pokestir"
    "COMPANY_WEBSITE \"https://pokestir.com\""
    "COMPANY_EMAIL \"contact@pokestir.com\""
    "BUNDLE_ID \"com.pokestir.juicy16\""
    "PLUGIN_MANUFACTURER_CODE Pkst"
    "PLUGIN_CODE Jc16"
    "PRODUCT_NAME \"Juicy16\"")
  string(FIND "${ROOT_CMAKE}" "${EXPECTED_IDENTITY}" IDENTITY_MATCH)
  if (IDENTITY_MATCH EQUAL -1)
    message(FATAL_ERROR "Approved Beta 1 identity missing from CMake: ${EXPECTED_IDENTITY}")
  endif ()
endforeach ()

file(READ "${PROJECT_SOURCE_DIR}/JuceLibraryCode/AppConfig.h" LEGACY_CONFIG)
string(FIND "${LEGACY_CONFIG}" "JucePlugin_VersionString          \"${PROJECT_VERSION}\"" LEGACY_VERSION)
string(FIND "${LEGACY_CONFIG}" "JucePlugin_Build_VST              0" LEGACY_VST2)
if (LEGACY_VERSION EQUAL -1 OR LEGACY_VST2 EQUAL -1)
  message(FATAL_ERROR "Quarantined Projucer fallback metadata drifted from CMake")
endif ()

if (EXISTS "${ARTIFACTS_DIR}/VST")
  message(FATAL_ERROR "Unsupported VST2 artifact is present in the normal build")
endif ()

message(STATUS
  "Metadata consistent: binary ${PROJECT_VERSION}, UI ${DISPLAY_VERSION}, AU/VST3 present, VST2 absent")
