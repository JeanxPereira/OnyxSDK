# -- OnyxBox -- synthetic example module proving the v1 module contracts --
# id "obx". No UI, no GPU -- just IGameModule + DecoderRegistry + TypeRegistrar
# wired against an invented container format. Tests/ links this to exercise
# Workspace end to end without depending on any real game format.

add_library(Onyx_ExampleBox STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/OnyxBoxModule.cpp
)
target_link_libraries(Onyx_ExampleBox PUBLIC Onyx::Core)
target_include_directories(Onyx_ExampleBox PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
