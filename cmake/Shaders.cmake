include(${PROJECT_SOURCE_DIR}/cmake/Deps/Glslang.cmake)

# onyx_add_spirv(<target> <shader>...)
# Compiles each GLSL shader to SPIR-V and embeds it as <Spirv/<Stem><Stage>.h>
# (Scene.vert -> Spirv/SceneVert.h, kSceneVert). The header directory is a
# PRIVATE include of <target>, and <target>_spirv orders the compile.
function(onyx_add_spirv target)
    onyx_dep_glslang()
    set(base_dir "${PROJECT_BINARY_DIR}/generated/spirv/${target}")
    set(header_dir "${base_dir}/Spirv")
    file(MAKE_DIRECTORY "${header_dir}")

    set(headers "")
    foreach(shader IN LISTS ARGN)
        cmake_path(ABSOLUTE_PATH shader BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE source)
        cmake_path(GET source STEM stem)
        cmake_path(GET source EXTENSION LAST_ONLY ext)
        if(ext STREQUAL ".vert")
            set(stage Vert)
        elseif(ext STREQUAL ".frag")
            set(stage Frag)
        else()
            message(FATAL_ERROR "onyx_add_spirv: unsupported shader stage '${ext}' for ${shader}")
        endif()
        set(symbol "${stem}${stage}")
        set(spv "${base_dir}/${symbol}.spv")
        set(header "${header_dir}/${symbol}.h")

        set(compiler_dep "")
        if(TARGET glslang-standalone)
            set(compiler_dep glslang-standalone)
        endif()
        add_custom_command(
            OUTPUT "${header}"
            COMMAND "${ONYX_GLSLANG}" --target-env vulkan1.3 --target-env spirv1.5 -o "${spv}" "${source}"
            COMMAND "${CMAKE_COMMAND}"
                    "-DINPUT_SPV=${spv}"
                    "-DOUTPUT_HEADER=${header}"
                    "-DSYMBOL=${symbol}"
                    -P "${PROJECT_SOURCE_DIR}/cmake/GenerateSpirvHeader.cmake"
            DEPENDS "${source}" "${PROJECT_SOURCE_DIR}/cmake/GenerateSpirvHeader.cmake" ${compiler_dep}
            COMMENT "Compiling ${shader} to SPIR-V"
            VERBATIM)
        list(APPEND headers "${header}")
    endforeach()

    add_custom_target(${target}_spirv DEPENDS ${headers})
    add_dependencies(${target} ${target}_spirv)
    target_include_directories(${target} PRIVATE "${base_dir}")
endfunction()
