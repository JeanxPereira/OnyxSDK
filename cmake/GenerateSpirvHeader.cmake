# Reads a compiled .spv file and emits a C++ header containing a
# `constexpr uint32_t` array of its words plus a byte-count constant.
# Invoked via `cmake -P` from onyx_add_spirv's custom command
# (cmake/ShaderCompile.cmake) -- never included/run directly.
#
# Inputs (set via -D on the `cmake -P` command line):
#   INPUT_SPV     path to the compiled .spv file
#   OUTPUT_HEADER path to write
#   ARRAY_NAME    C identifier stem in snake_case, e.g. "scene_vert"
#                 -> array name kSceneVertSpv

if(NOT DEFINED INPUT_SPV OR NOT DEFINED OUTPUT_HEADER OR NOT DEFINED ARRAY_NAME)
    message(FATAL_ERROR
        "GenerateSpirvHeader.cmake requires -DINPUT_SPV=... -DOUTPUT_HEADER=... -DARRAY_NAME=...")
endif()

file(READ "${INPUT_SPV}" _hex HEX)
string(LENGTH "${_hex}" _hexLen)
math(EXPR _byteCount "${_hexLen} / 2")

if(NOT _byteCount GREATER 0)
    message(FATAL_ERROR "GenerateSpirvHeader.cmake: ${INPUT_SPV} is empty")
endif()
math(EXPR _wordRemainder "${_byteCount} % 4")
if(NOT _wordRemainder EQUAL 0)
    message(FATAL_ERROR "GenerateSpirvHeader.cmake: ${INPUT_SPV} size ${_byteCount} is not a multiple of 4")
endif()
math(EXPR _wordCount "${_byteCount} / 4")

# snake_case -> PascalCase (e.g. "scene_vert" -> "SceneVert") for the
# generated array's C++ identifier.
string(REPLACE "_" ";" _parts "${ARRAY_NAME}")
set(_pascal "")
foreach(_part ${_parts})
    string(SUBSTRING "${_part}" 0 1 _first)
    string(SUBSTRING "${_part}" 1 -1 _rest)
    string(TOUPPER "${_first}" _first)
    string(APPEND _pascal "${_first}${_rest}")
endforeach()

# SPIR-V words are little-endian; file(READ HEX) yields bytes in file
# order, so each word's 4 hex-byte-pairs must be reversed to form the
# 0xAABBCCDD literal a C++ compiler reads as that same little-endian word.
set(_body "")
set(_i 0)
while(_i LESS _wordCount)
    math(EXPR _off "${_i} * 8")
    string(SUBSTRING "${_hex}" ${_off} 2 _b0)
    math(EXPR _off1 "${_off} + 2")
    string(SUBSTRING "${_hex}" ${_off1} 2 _b1)
    math(EXPR _off2 "${_off} + 4")
    string(SUBSTRING "${_hex}" ${_off2} 2 _b2)
    math(EXPR _off3 "${_off} + 6")
    string(SUBSTRING "${_hex}" ${_off3} 2 _b3)
    string(APPEND _body "0x${_b3}${_b2}${_b1}${_b0}u,")

    math(EXPR _i "${_i} + 1")
    math(EXPR _col "${_i} % 8")
    if(_col EQUAL 0)
        string(APPEND _body "\n    ")
    endif()
endwhile()

set(_content "// Generated at build time by cmake/GenerateSpirvHeader.cmake from
// ${INPUT_SPV}. Do NOT edit; do NOT commit (build-tree only, never
// checked in -- see onyx_add_spirv in cmake/ShaderCompile.cmake).
#pragma once
#include <cstdint>

namespace Onyx::RenderVk::Shaders {

constexpr uint32_t k${_pascal}Spv[] = {
    ${_body}
};
constexpr uint32_t k${_pascal}SpvSize = sizeof(k${_pascal}Spv);

} // namespace Onyx::RenderVk::Shaders
")

file(WRITE "${OUTPUT_HEADER}" "${_content}")
