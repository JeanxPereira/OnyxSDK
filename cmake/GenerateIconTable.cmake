# Generates the { "ICON_SF_NAME", ICON_SF_NAME } table used by the icon browser
# straight from SFSymbols.h, so the two can never drift. Previously the same
# 6.6k entries were maintained by hand inside FontDebuggerWindow.cpp.
#
# Invoked as a script: cmake -DHEADER=<in> -DOUTPUT=<out> -P GenerateIconTable.cmake

file(STRINGS "${HEADER}" LINES REGEX "^#define[ \t]+ICON_SF_[A-Z0-9_]+[ \t]+\"")

set(BODY "// Generated from Onyx/Fonts/SFSymbols.h by cmake/GenerateIconTable.cmake.\n// Do not edit -- regenerate by touching the header and rebuilding.\n")
foreach(LINE IN LISTS LINES)
    string(REGEX MATCH "^#define[ \t]+(ICON_SF_[A-Z0-9_]+)" _m "${LINE}")
    string(APPEND BODY "{ \"${CMAKE_MATCH_1}\", ${CMAKE_MATCH_1} },\n")
endforeach()

list(LENGTH LINES COUNT)
if(COUNT LESS 100)
    message(FATAL_ERROR "GenerateIconTable: only ${COUNT} ICON_SF_* defines found in "
                        "${HEADER} -- the header format probably changed.")
endif()

file(WRITE "${OUTPUT}" "${BODY}")
message(STATUS "GenerateIconTable: wrote ${COUNT} icons to ${OUTPUT}")
