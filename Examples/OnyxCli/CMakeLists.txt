# -- OnyxCli -- headless probe/list/extract/decode CLI over OnyxBox --------
# Proves Task 6's Onyx::Cli::Run dispatcher against the synthetic OnyxBox
# module. No UI, no GPU: links only Onyx_Core + Onyx_ExampleBox.

add_executable(onyxbox-cli
    ${CMAKE_CURRENT_SOURCE_DIR}/Main.cpp
)
target_link_libraries(onyxbox-cli PRIVATE Onyx_Core Onyx_ExampleBox)
