# -- OnyxConsumerGate -- the fake external consumer -------------------------
# Stands in for a project that pulls Onyx in exactly the way README's
# "Consuming Onyx" section documents: add_subdirectory()/FetchContent against
# the repo root, then target_link_libraries(... Onyx::Onyx). The whole point
# of this file living OUTSIDE the Onyx source tree (Tests/Consumer/ is inside
# it on disk, but this project() call makes CMAKE_SOURCE_DIR its own --
# ONYX_SOURCE_DIR below -- once ExternalConsumptionTest.cmake configures it
# as its own top-level project into a separate build tree) is to reproduce
# the exact condition Tools/ColdStart cannot: CMAKE_SOURCE_DIR inside Onyx's
# own cmake/ files pointing at THIS project, not at Onyx, the moment Onyx is
# add_subdirectory()'d from here.
#
# ONYX_SOURCE_DIR is supplied via -D by ExternalConsumptionTest.cmake (the
# outer, real Onyx build's own ONYX_ROOT_DIR, passed through) rather than a
# relative ../../.. guess, so this project has no assumption baked in about
# where it lives relative to the Onyx checkout it is consuming -- the same
# freedom a real consumer's FetchContent_Declare(GIT_REPOSITORY ...) has.
cmake_minimum_required(VERSION 3.20)
project(OnyxConsumerGate LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT DEFINED ONYX_SOURCE_DIR)
    message(FATAL_ERROR
        "OnyxConsumerGate: ONYX_SOURCE_DIR not set -- pass "
        "-DONYX_SOURCE_DIR=<path to the OnyxSDK repo root> (the path this "
        "project add_subdirectory()s, standing in for FetchContent's "
        "GIT_REPOSITORY checkout).")
endif()

# Trim the nested configure to what an app actually needs to link
# Onyx::Onyx: no examples, no Onyx test suite (which would try to
# add_subdirectory(Tests/Consumer) right back into itself), no FFmpeg
# fetch/build. None of these affect Onyx::Onyx's own link closure --
# see README's target table -- so turning them off changes nothing this
# gate is trying to prove, only how much of the tree it has to build to
# prove it.
set(ONYX_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ONYX_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
set(ONYX_COMPONENT_MEDIA OFF CACHE BOOL "" FORCE)

# The consumption model under test: add_subdirectory() against a path
# outside this project's own source tree, exactly like a vendored checkout
# (git submodule/subtree) or FetchContent_MakeAvailable() would end up
# doing -- both call this same root CMakeLists.txt. A second BINARY_DIR
# argument is required because ONYX_SOURCE_DIR is not a subdirectory of
# ${CMAKE_SOURCE_DIR} here.
add_subdirectory("${ONYX_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/onyx-sdk")

add_executable(consumer_app main.cpp)
target_link_libraries(consumer_app PRIVATE Onyx::Onyx)
