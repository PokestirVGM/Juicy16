# Drift guard for the vendored libsndfile IRCAM hardening patch.
#
# The patch is applied by tools/build_macos_dependencies.sh and
# tools/build_windows_dependencies.ps1, neither of which CMake runs, and both of
# which need network access. This test cannot rebuild the closure, so it checks
# the thing that can silently rot instead: that the patch file, the two recipes,
# and the README all still agree on the same three hashes, and that the patch
# still makes both edits it is supposed to make.
#
# Rationale and the upstream provenance of the changes are in
# vendor/libsndfile_patched/README.md.

if (NOT DEFINED SOURCE_ROOT)
  message(FATAL_ERROR "Dependency patch test requires -DSOURCE_ROOT")
endif ()

# Reviewed values. Changing the patch means regenerating all three and rerunning
# the dependency recipe.
set(PATCH_SHA256 "9ab039a1261c8705f7238876d7ec634d375ddb78ac72a3676b9216e10f88995b")
set(IRCAM_BASE_SHA256 "52fab7073b1c7716902ee217769a48117577c1f33e84fb038232e2fe41088470")
set(IRCAM_PATCHED_SHA256 "27c25a5938d0c2571f9aaf0910ecedee57c440e62be66cf55f7708fa5ba3a1ab")

set(PATCH_FILE
    "${SOURCE_ROOT}/vendor/libsndfile_patched/libsndfile-1.2.2-ircam-hardening.patch")
set(PATCH_README "${SOURCE_ROOT}/vendor/libsndfile_patched/README.md")
set(MACOS_RECIPE "${SOURCE_ROOT}/tools/build_macos_dependencies.sh")
set(WINDOWS_RECIPE "${SOURCE_ROOT}/tools/build_windows_dependencies.ps1")

foreach (REQUIRED IN ITEMS "${PATCH_FILE}" "${PATCH_README}" "${MACOS_RECIPE}" "${WINDOWS_RECIPE}")
  if (NOT EXISTS "${REQUIRED}")
    message(FATAL_ERROR "Missing required dependency-patch input: ${REQUIRED}")
  endif ()
endforeach ()

file(SHA256 "${PATCH_FILE}" ACTUAL_PATCH_SHA256)
if (NOT ACTUAL_PATCH_SHA256 STREQUAL PATCH_SHA256)
  message(FATAL_ERROR
    "libsndfile IRCAM patch changed without updating the reviewed hash: "
    "expected ${PATCH_SHA256}, got ${ACTUAL_PATCH_SHA256}. "
    "Rebuild the dependency closure and update both recipes and the README.")
endif ()

# The patch must still target src/ircam.c and make both edits. A patch that
# applies cleanly but no longer contains the security change would pass a hash
# check only if the hash were updated to match it, so check the content too.
file(READ "${PATCH_FILE}" PATCH_TEXT)
# Trailing " ;" is omitted from the two code lines: a literal semicolon cannot
# survive a CMake IN ITEMS list, and the prefix is already unambiguous.
foreach (REQUIRED_LINE IN ITEMS
    "--- a/src/ircam.c"
    "+++ b/src/ircam.c"
    "-\tpsf->sf.samplerate = (int) samplerate"
    "+\tpsf->sf.samplerate = psf_lrintf (samplerate)")
  string(FIND "${PATCH_TEXT}" "${REQUIRED_LINE}" FOUND_AT)
  if (FOUND_AT EQUAL -1)
    message(FATAL_ERROR "libsndfile IRCAM patch no longer contains: ${REQUIRED_LINE}")
  endif ()
endforeach ()

# Both channel-count checks - the little-endian read and the big-endian retry -
# must gain the lower bound, or a zero/negative count still reaches the
# blockwidth multiplication.
string(REGEX MATCHALL "\\+[^\n]*psf->sf\\.channels < 1 \\|\\| psf->sf\\.channels > SF_MAX_CHANNELS"
       CHANNEL_GUARDS "${PATCH_TEXT}")
list(LENGTH CHANNEL_GUARDS CHANNEL_GUARD_COUNT)
if (NOT CHANNEL_GUARD_COUNT EQUAL 2)
  message(FATAL_ERROR
    "libsndfile IRCAM patch must add the lower channel bound to both the "
    "little-endian read and the big-endian retry; found ${CHANNEL_GUARD_COUNT}.")
endif ()

function(assert_contains LABEL FILE_PATH NEEDLE)
  file(READ "${FILE_PATH}" HAYSTACK)
  string(FIND "${HAYSTACK}" "${NEEDLE}" FOUND_AT)
  if (FOUND_AT EQUAL -1)
    message(FATAL_ERROR "${LABEL} no longer references '${NEEDLE}'")
  endif ()
endfunction()

# macOS applies the diff, bracketed by the same pre- and post-edit file hashes.
assert_contains("The macOS dependency recipe" "${MACOS_RECIPE}"
                "libsndfile-1.2.2-ircam-hardening.patch")
assert_contains("The macOS dependency recipe" "${MACOS_RECIPE}" "${PATCH_SHA256}")
assert_contains("The macOS dependency recipe" "${MACOS_RECIPE}" "${IRCAM_BASE_SHA256}")
assert_contains("The macOS dependency recipe" "${MACOS_RECIPE}" "${IRCAM_PATCHED_SHA256}")

# Windows has no guaranteed patch.exe, so it substitutes the same text and
# brackets the edit with the pre- and post-edit file hashes instead.
assert_contains("The Windows dependency recipe" "${WINDOWS_RECIPE}" "Repair-SndfileIrcam")
assert_contains("The Windows dependency recipe" "${WINDOWS_RECIPE}" "${IRCAM_BASE_SHA256}")
assert_contains("The Windows dependency recipe" "${WINDOWS_RECIPE}" "${IRCAM_PATCHED_SHA256}")

foreach (RECORDED_HASH IN ITEMS
    "${PATCH_SHA256}" "${IRCAM_BASE_SHA256}" "${IRCAM_PATCHED_SHA256}")
  assert_contains("vendor/libsndfile_patched/README.md" "${PATCH_README}" "${RECORDED_HASH}")
endforeach ()

message(STATUS "libsndfile IRCAM hardening patch, both recipes, and the README agree")

# Both platforms must download the same reviewed synthesis engine archive.
foreach (recipe tools/build_macos_dependencies.sh tools/build_windows_dependencies.ps1)
  file(READ "${SOURCE_ROOT}/${recipe}" ENGINE_RECIPE)
  string(FIND "${ENGINE_RECIPE}" "ce27840221ab00dd59bf27e85ecbba480c6c2a7c9fbec4243658f68f59c07f4a" ENGINE_HASH_AT)
  string(FIND "${ENGINE_RECIPE}" "2.5.7" ENGINE_VERSION_AT)
  if (ENGINE_HASH_AT LESS 0 OR ENGINE_VERSION_AT LESS 0)
    message(FATAL_ERROR "FluidSynth 2.5.7 pin/checksum drift in ${recipe}")
  endif ()
endforeach ()

# Both platforms share the exact reviewed CC1 vibrato extension.
file(SHA256 "${SOURCE_ROOT}/vendor/fluidsynth_patched/apply.cmake" VIBRATO_HASH)
if(NOT VIBRATO_HASH STREQUAL "5652f9b49f23927da025a69c5922078c09430fa9ba671775980633adcbf54689")
  message(FATAL_ERROR "Unreviewed vibrato apply.cmake; refresh evidence when changing the engine patch")
endif()
file(SHA256 "${SOURCE_ROOT}/vendor/fluidsynth_patched/cc1-vibrato-scale.patch" VIBRATO_HASH)
if(NOT VIBRATO_HASH STREQUAL "ad2549f3b64b2ce8bba850656e45e45583c8fdc3b601f7212f0832c3322f9f8d")
  message(FATAL_ERROR "Unreviewed vibrato cc1-vibrato-scale.patch; refresh evidence when changing the engine patch")
endif()
foreach(recipe "${MACOS_RECIPE}" "${WINDOWS_RECIPE}")
  assert_contains("FluidSynth dependency recipe" "${recipe}" "vendor/fluidsynth_patched/apply.cmake")
endforeach()
