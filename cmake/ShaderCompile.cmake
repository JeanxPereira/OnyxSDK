# onyx_add_spirv(<target> <shader1.vert|frag> [<shader2...>] ...)
#
# For each GLSL 450 shader source (path relative to CMAKE_SOURCE_DIR),
# compiles it to SPIR-V at build time via glslang's standalone compiler
# (the `glslang-standalone` CMake target, binary OUTPUT_NAME `glslang`;
# see .superpowers/sdd/2026-08-19-onyx-v1-m4-vulkan/task-1-report.md) and
# embeds the result as a generated C++ header
# (${CMAKE_BINARY_DIR}/generated/shaders/<name>_spv.h, a
# `constexpr uint32_t k<PascalName>Spv[]` array plus a
# `k<PascalName>SpvSize` byte count — see GenerateSpirvHeader.cmake).
# Nothing is loaded from a file at runtime.
#
# <target> gets:
#   - the generated header directory added to its PUBLIC include path;
#   - a dependency (via the `<target>_spirv` custom target) that forces
#     every shader to compile before <target> does.
# <target> must NOT list the .vert/.frag sources in its own source list —
# they never compile as C++ and ONYX_RENDER_SOURCES' completeness check
# only globs Source/*.cpp, so this is automatic, not something the caller
# has to remember.
#
# The .spv and generated .h files land in the build tree only — never
# committed (matches every other generated-header pattern in this repo,
# e.g. ONYX_ICON_TABLE).
function(onyx_add_spirv target)
    set(_gen_dir "${CMAKE_BINARY_DIR}/generated/shaders")
    file(MAKE_DIRECTORY "${_gen_dir}")

    set(_headers "")
    foreach(_shader ${ARGN})
        get_filename_component(_name "${_shader}" NAME)      # scene.vert
        string(REPLACE "." "_" _safe_name "${_name}")         # scene_vert
        set(_source "${CMAKE_SOURCE_DIR}/${_shader}")
        set(_spv    "${_gen_dir}/${_safe_name}.spv")
        set(_header "${_gen_dir}/${_safe_name}_spv.h")

        add_custom_command(
            OUTPUT "${_header}"
            COMMAND "$<TARGET_FILE:glslang-standalone>"
                    --target-env vulkan1.3
                    -o "${_spv}"
                    "${_source}"
            COMMAND "${CMAKE_COMMAND}"
                    "-DINPUT_SPV=${_spv}"
                    "-DOUTPUT_HEADER=${_header}"
                    "-DARRAY_NAME=${_safe_name}"
                    -P "${CMAKE_SOURCE_DIR}/cmake/GenerateSpirvHeader.cmake"
            DEPENDS "${_source}"
                    glslang-standalone
                    "${CMAKE_SOURCE_DIR}/cmake/GenerateSpirvHeader.cmake"
            COMMENT "onyx_add_spirv: ${_shader} -> ${_safe_name}_spv.h"
            VERBATIM)
        list(APPEND _headers "${_header}")
    endforeach()

    set(_spirv_target "${target}_spirv")
    add_custom_target(${_spirv_target} DEPENDS ${_headers})
    add_dependencies(${target} ${_spirv_target})
    target_include_directories(${target} PUBLIC "${_gen_dir}")
endfunction()
