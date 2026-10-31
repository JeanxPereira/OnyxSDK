# Generates the { "ICON_SF_NAME", ICON_SF_NAME } table straight from SfSymbols.h so the two
# cannot drift.
#
# Invoked as a script: cmake -DHEADER=<in> -DOUTPUT=<out> -P GenerateIconTable.cmake

file(STRINGS "${HEADER}" LINES REGEX "^#define[ \t]+ICON_SF_[A-Z0-9_]+[ \t]+\"")

set(BODY "// Generated from Onyx/Ui/SfSymbols.h by cmake/GenerateIconTable.cmake. Do not edit.\n")
string(APPEND BODY "#include \"Onyx/Ui/Icons.h\"\n#include \"Onyx/Ui/SfSymbols.h\"\n\n")
string(APPEND BODY "namespace Onyx::Ui {\n\nstd::span<const IconEntry> IconTable() {\n")
string(APPEND BODY "    static const IconEntry kTable[] = {\n")
foreach(LINE IN LISTS LINES)
    string(REGEX MATCH "^#define[ \t]+(ICON_SF_[A-Z0-9_]+)" _m "${LINE}")
    string(APPEND BODY "        { \"${CMAKE_MATCH_1}\", ${CMAKE_MATCH_1} },\n")
endforeach()
string(APPEND BODY "    };\n    return kTable;\n}\n\n}\n")

list(LENGTH LINES COUNT)
if(COUNT LESS 100)
    message(FATAL_ERROR "GenerateIconTable: only ${COUNT} ICON_SF_* defines found in "
                        "${HEADER} -- the header format probably changed.")
endif()

file(WRITE "${OUTPUT}" "${BODY}")
message(STATUS "GenerateIconTable: wrote ${COUNT} icons to ${OUTPUT}")
