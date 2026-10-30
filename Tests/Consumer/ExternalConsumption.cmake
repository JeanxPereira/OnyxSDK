foreach(var ONYX_ROOT CONSUMER_SRC WORK GENERATOR)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "ExternalConsumption.cmake: ${var} not set")
    endif()
endforeach()
if(NOT BUILD_TYPE)
    set(BUILD_TYPE Debug)
endif()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

set(configure_args
    -S "${CONSUMER_SRC}"
    -B "${WORK}"
    -G "${GENERATOR}"
    "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}"
    "-DCMAKE_CXX_SCAN_FOR_MODULES=OFF"
    "-DFETCHCONTENT_SOURCE_DIR_ONYXSDK=${ONYX_ROOT}")
if(C_COMPILER)
    list(APPEND configure_args "-DCMAKE_C_COMPILER=${C_COMPILER}")
endif()
if(CXX_COMPILER)
    list(APPEND configure_args "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}")
endif()

function(run_step label)
    execute_process(COMMAND ${ARGN}
        RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    message(STATUS "${out}")
    if(err)
        message(STATUS "${err}")
    endif()
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "Consumer.CoreOnly: ${label} failed (exit ${rc})")
    endif()
    set(step_output "${out}" PARENT_SCOPE)
endfunction()

run_step(configure ${CMAKE_COMMAND} ${configure_args})
run_step(build ${CMAKE_COMMAND} --build "${WORK}" --parallel 4)

find_program(consumer_exe CoreOnlyConsumer PATHS "${WORK}" NO_DEFAULT_PATH)
if(NOT consumer_exe)
    set(consumer_exe "${WORK}/CoreOnlyConsumer${CMAKE_EXECUTABLE_SUFFIX}")
endif()
run_step(run "${consumer_exe}")
if(NOT step_output MATCHES "Consumer ok")
    message(FATAL_ERROR "Consumer.CoreOnly: the executable did not print its success line")
endif()

set(forbidden volk vulkan vma glslang imgui glfw ffmpeg implot)
file(GLOB deps LIST_DIRECTORIES true RELATIVE "${WORK}/_deps" "${WORK}/_deps/*")
set(has_glm FALSE)
foreach(dep IN LISTS deps)
    if(dep MATCHES "^glm")
        set(has_glm TRUE)
    endif()
endforeach()
if(NOT has_glm)
    message(FATAL_ERROR
        "Consumer.CoreOnly: no glm directory under ${WORK}/_deps (${deps}); "
        "the dependency check below would pass vacuously")
endif()

set(found "")
foreach(dep IN LISTS deps)
    foreach(prefix IN LISTS forbidden)
        string(TOLOWER "${dep}" lowered)
        if(lowered MATCHES "^${prefix}")
            list(APPEND found "${dep}")
        endif()
    endforeach()
endforeach()
if(found)
    message(FATAL_ERROR
        "Consumer.CoreOnly: a Core-only configure fetched GPU or UI dependencies: ${found}")
endif()
message(STATUS "Consumer.CoreOnly: no GPU or UI dependency under ${WORK}/_deps (${deps})")
