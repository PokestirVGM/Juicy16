if (NOT DEFINED PROJECT_SOURCE_DIR OR NOT DEFINED ARTIFACTS_DIR
    OR NOT DEFINED PROJECT_VERSION OR NOT DEFINED DISPLAY_VERSION)
  message(FATAL_ERROR "Metadata test arguments are incomplete")
endif ()

set(AU_PLIST
    "${ARTIFACTS_DIR}/AU/JuicySF Rack.component/Contents/Info.plist")
set(VST3_INFO
    "${ARTIFACTS_DIR}/VST3/JuicySF Rack.vst3/Contents/Resources/moduleinfo.json")
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

file(READ "${VST3_INFO}" VST3_METADATA)
string(REGEX MATCH "\"Version\"[ \t]*:[ \t]*\"${PROJECT_VERSION}\"" VST3_VERSION_MATCH
             "${VST3_METADATA}")
if (NOT VST3_VERSION_MATCH)
  message(FATAL_ERROR "VST3 moduleinfo does not contain version '${PROJECT_VERSION}'")
endif ()

file(READ "${PROJECT_SOURCE_DIR}/CMakeLists.txt" ROOT_CMAKE)
string(FIND "${ROOT_CMAKE}" "JUICYSF_RACK_VERSION=\"\${JUICYSF_DISPLAY_VERSION}\"" DISPLAY_WIRING)
if (DISPLAY_WIRING EQUAL -1)
  message(FATAL_ERROR "UI display version is no longer wired to JUICYSF_DISPLAY_VERSION")
endif ()

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
