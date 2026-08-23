# The committed host-test MIDI fixtures must be exactly what
# tools/make_host_fixtures.py emits.
#
# docs/HOST_TEST_PROTOCOL.md tabulates the expected instrument on every channel
# at every checkpoint, and those tables are read off the generator. If someone
# edits the generator without regenerating, or edits a .mid by hand, the tester
# is verifying against a table that no longer describes the file they imported —
# and the failure would look like a host bug.

if (NOT DEFINED SOURCE_ROOT OR NOT DEFINED PYTHON_EXECUTABLE)
  message(FATAL_ERROR "Host fixture test requires -DSOURCE_ROOT and -DPYTHON_EXECUTABLE")
endif ()

set(FIXTURE_DIR "${SOURCE_ROOT}/tests/fixtures/host")
set(SCRATCH "${CMAKE_CURRENT_BINARY_DIR}/host-fixture-regen")
file(REMOVE_RECURSE "${SCRATCH}")
file(MAKE_DIRECTORY "${SCRATCH}")

execute_process(
  COMMAND "${PYTHON_EXECUTABLE}" "${SOURCE_ROOT}/tools/make_host_fixtures.py" "${SCRATCH}"
  RESULT_VARIABLE GENERATE_RESULT
  OUTPUT_VARIABLE GENERATE_OUTPUT
  ERROR_VARIABLE GENERATE_ERROR)
if (NOT GENERATE_RESULT EQUAL 0)
  message(FATAL_ERROR
    "tools/make_host_fixtures.py failed (${GENERATE_RESULT}):\n${GENERATE_OUTPUT}${GENERATE_ERROR}")
endif ()

foreach (FIXTURE IN ITEMS host_program_matrix host_controllers)
  set(COMMITTED "${FIXTURE_DIR}/${FIXTURE}.mid")
  set(REGENERATED "${SCRATCH}/${FIXTURE}.mid")
  if (NOT EXISTS "${COMMITTED}")
    message(FATAL_ERROR "Missing committed host fixture: ${COMMITTED}")
  endif ()
  file(SHA256 "${COMMITTED}" COMMITTED_SHA)
  file(SHA256 "${REGENERATED}" REGENERATED_SHA)
  if (NOT COMMITTED_SHA STREQUAL REGENERATED_SHA)
    message(FATAL_ERROR
      "${FIXTURE}.mid does not match tools/make_host_fixtures.py "
      "(committed ${COMMITTED_SHA}, generated ${REGENERATED_SHA}). "
      "Rerun the generator and re-read the tables in docs/HOST_TEST_PROTOCOL.md.")
  endif ()
endforeach ()

file(REMOVE_RECURSE "${SCRATCH}")
message(STATUS "Host-test MIDI fixtures match their generator")
