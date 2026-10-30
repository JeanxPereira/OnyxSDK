cmake_minimum_required(VERSION 3.28)
project(CoreOnlyConsumer CXX C)

include(FetchContent)
FetchContent_Declare(OnyxSDK
    GIT_REPOSITORY https://github.com/JeanxPereira/OnyxSDK.git
    GIT_TAG        v2)

set(ONYX_WITH_GPU OFF)
set(ONYX_BUILD_TESTS OFF)
set(ONYX_BUILD_EXAMPLES OFF)
FetchContent_MakeAvailable(OnyxSDK)

add_subdirectory("${onyxsdk_SOURCE_DIR}/Examples/OnyxBox" "${CMAKE_BINARY_DIR}/OnyxBox")

add_executable(CoreOnlyConsumer Main.cpp)
set_target_properties(CoreOnlyConsumer PROPERTIES CXX_SCAN_FOR_MODULES OFF)
target_link_libraries(CoreOnlyConsumer PRIVATE Onyx::Core Onyx::ExampleBox)
