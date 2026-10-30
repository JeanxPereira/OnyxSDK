# LayerGuard - fails when a layer's file includes something its layer forbids.
# Invoked as:
#   cmake -DLISTFILE=<file with one path per line> -DFORBIDDEN=<regex> -P LayerGuard.cmake
#
# The lists are generated at configure time from the layer manifests in the
# top-level CMakeLists, plus each source's same-name header under Include/.
# Link errors already police symbol-level layering; this catches the quieter
# failure of a header-only leak that compiles today and traps us later.
file(STRINGS "${LISTFILE}" _files)
set(_bad "")
foreach(_f ${_files})
    if(NOT EXISTS "${_f}")
        continue()
    endif()
    file(STRINGS "${_f}" _hits REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"](${FORBIDDEN})")
    if(_hits)
        list(APPEND _bad "${_f}: ${_hits}")
    endif()
endforeach()
if(_bad)
    list(JOIN _bad "\n" _msg)
    message(FATAL_ERROR "Layer violation(s):\n${_msg}")
endif()
