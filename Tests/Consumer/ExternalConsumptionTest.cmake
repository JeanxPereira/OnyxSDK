# -- ExternalConsumptionTest.cmake -- the ExternalConsumption gate ----------
#
# Configures and builds Tests/Consumer/consumer_project as its OWN top-level
# CMake project, out-of-tree, add_subdirectory()-ing this Onyx checkout from
# outside its source tree -- the cheap, network-free stand-in for a real
# FetchContent_Declare(GIT_REPOSITORY ...) consumer that this repo's own CI
# cannot assume has network access to itself. This is the proof the fix for
# CMAKE_SOURCE_DIR-in-an-included-.cmake-file (cmake/ShaderCompile.cmake,
# the two LayerGuard ctest entries in the root CMakeLists.txt, Tests/
# CMakeLists.txt's OnyxOracle source references, Tools/OnyxOracle/
# CMakeLists.txt) actually holds: every one of those sites now resolves
# through ONYX_ROOT_DIR, captured once from CMAKE_CURRENT_SOURCE_DIR at the
# top of the root CMakeLists.txt, specifically because CMAKE_SOURCE_DIR
# itself is the *outer* project's root once Onyx is nested -- which is
# exactly the configuration this script creates.
#
# Demonstrated failing against the unfixed code (path fix stashed, this gate
# run, restored) before being trusted here -- see the task report for both
# transcripts. Without that demonstration a passing gate proves nothing: a
# gate that could not have caught the bug it exists to catch is not a gate.
#
# ONYX_ROOT, CONSUMER_SRC, WORK, GENERATOR, BUILD_TYPE are passed in via -D
# from the add_test COMMAND in CMakeLists.txt. DEPS_CACHE is optional --
# when set, it points the nested configure's FETCHCONTENT_BASE_DIR at this
# (outer, real) build's own _deps directory, so glfw/glm/vma/vulkan-headers/
# glslang/cgltf/tomlplusplus (every FetchContent_Declare in the root
# CMakeLists.txt uses GIT_SHALLOW + UPDATE_DISCONNECTED) are found already
# populated and are not re-fetched over the network for this gate at all --
# the same reuse a developer's own second build tree gets for free.

if(NOT DEFINED ONYX_ROOT)
    message(FATAL_ERROR "ExternalConsumptionTest.cmake: ONYX_ROOT not set (pass -DONYX_ROOT=<path>)")
endif()
if(NOT DEFINED CONSUMER_SRC)
    message(FATAL_ERROR "ExternalConsumptionTest.cmake: CONSUMER_SRC not set (pass -DCONSUMER_SRC=<path>)")
endif()
if(NOT DEFINED WORK)
    message(FATAL_ERROR "ExternalConsumptionTest.cmake: WORK not set (pass -DWORK=<dir>)")
endif()
if(NOT DEFINED GENERATOR)
    set(GENERATOR "Ninja")
endif()
if(NOT DEFINED BUILD_TYPE OR BUILD_TYPE STREQUAL "")
    set(BUILD_TYPE "Debug")
endif()

# Fresh out-of-tree build directory every run -- this is exactly what "out-
# of-tree" is testing: WORK is neither inside CONSUMER_SRC nor inside
# ONYX_ROOT, and did not exist before this script ran.
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

set(_configure_args
    -S "${CONSUMER_SRC}"
    -B "${WORK}"
    -G "${GENERATOR}"
    "-DONYX_SOURCE_DIR=${ONYX_ROOT}"
    "-DCMAKE_BUILD_TYPE=${BUILD_TYPE}")
if(DEPS_CACHE)
    list(APPEND _configure_args "-DFETCHCONTENT_BASE_DIR=${DEPS_CACHE}")
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND} ${_configure_args}
    RESULT_VARIABLE _configure_rc
    OUTPUT_VARIABLE _configure_out
    ERROR_VARIABLE _configure_err)
if(_configure_out)
    message(STATUS "${_configure_out}")
endif()
if(_configure_err)
    message(STATUS "${_configure_err}")
endif()
if(NOT _configure_rc EQUAL 0)
    message(FATAL_ERROR
        "ExternalConsumption: nested configure failed (exit ${_configure_rc}) "
        "-- Onyx cannot be configured via add_subdirectory() from outside "
        "its own source tree, which is the one consumption model README's "
        "\"Consuming Onyx\" section promises v1 supports.")
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND} --build "${WORK}" --target consumer_app
    RESULT_VARIABLE _build_rc
    OUTPUT_VARIABLE _build_out
    ERROR_VARIABLE _build_err)
if(_build_out)
    message(STATUS "${_build_out}")
endif()
if(_build_err)
    message(STATUS "${_build_err}")
endif()
if(NOT _build_rc EQUAL 0)
    message(FATAL_ERROR
        "ExternalConsumption: nested build failed (exit ${_build_rc}) -- "
        "Onyx configured but failed to build once nested; this is the "
        "failure mode a path resolved through CMAKE_SOURCE_DIR instead of "
        "ONYX_ROOT_DIR produces (it resolves inside the CONSUMER's tree, "
        "not Onyx's own).")
endif()

message(STATUS
    "ExternalConsumption: consumer_app configured and built with Onyx "
    "pulled in via add_subdirectory() from outside the Onyx source tree "
    "(${ONYX_ROOT} nested under ${WORK})")
