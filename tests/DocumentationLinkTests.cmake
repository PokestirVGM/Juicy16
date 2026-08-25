if (NOT DEFINED SOURCE_ROOT OR NOT IS_DIRECTORY "${SOURCE_ROOT}")
  message(FATAL_ERROR "SOURCE_ROOT must name the Juicy16 source directory")
endif ()

file(GLOB_RECURSE _markdown_files LIST_DIRECTORIES FALSE "${SOURCE_ROOT}/*.md")
set(_broken_links "")
set(_checked_links 0)
set(_checked_documents 0)

foreach (_document IN LISTS _markdown_files)
  # Matched on the path relative to SOURCE_ROOT, not the absolute one, so that a
  # staged package living under distribute/out can be handed to this script as
  # its own SOURCE_ROOT and still be checked.
  file(RELATIVE_PATH _relative_document "${SOURCE_ROOT}" "${_document}")
  if (_relative_document MATCHES "^(build[^/]*|testfiles|distribute/out)/")
    continue()
  endif ()

  file(READ "${_document}" _contents)
  get_filename_component(_document_dir "${_document}" DIRECTORY)
  math(EXPR _checked_documents "${_checked_documents} + 1")

  # Scanned with an explicit cursor rather than string(REGEX MATCHALL): MATCHALL
  # returns its matches with the separators escaped, so `foreach (IN LISTS)` sees
  # one element and only the first link in each file was ever resolved. That hole
  # hid two dead links in ROADMAP.md through every previously "passing" run.
  set(_rest "${_contents}")
  while (TRUE)
    string(REGEX MATCH "\\]\\(([^)]+)\\)" _match "${_rest}")
    if (_match STREQUAL "")
      break()
    endif ()
    set(_target "${CMAKE_MATCH_1}")

    string(FIND "${_rest}" "${_match}" _match_start)
    string(LENGTH "${_match}" _match_length)
    math(EXPR _cursor "${_match_start} + ${_match_length}")
    string(SUBSTRING "${_rest}" ${_cursor} -1 _rest)

    if (_target MATCHES "^(https?://|mailto:|#)")
      continue()
    endif ()

    string(REGEX REPLACE "#.*$" "" _target "${_target}")
    string(REPLACE "%20" " " _target "${_target}")
    if (_target STREQUAL "")
      continue()
    endif ()

    math(EXPR _checked_links "${_checked_links} + 1")
    if (IS_ABSOLUTE "${_target}")
      set(_resolved "${SOURCE_ROOT}${_target}")
    else ()
      get_filename_component(_resolved "${_document_dir}/${_target}" ABSOLUTE)
    endif ()
    if (NOT EXISTS "${_resolved}")
      list(APPEND _broken_links "${_relative_document} -> ${_target}")
    endif ()
  endwhile ()
endforeach ()

if (_broken_links)
  list(JOIN _broken_links "\n  " _broken_text)
  message(FATAL_ERROR "Broken internal documentation links:\n  ${_broken_text}")
endif ()

message(STATUS
  "Checked ${_checked_links} internal links across ${_checked_documents} Markdown files")
