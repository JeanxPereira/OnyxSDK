add_executable(OnyxCli Main.cpp)
target_link_libraries(OnyxCli PRIVATE Onyx::Cli Onyx::Render Onyx::Exchange Onyx::ExampleBox Onyx::ExamplePrism)
onyx_warnings(OnyxCli)
set_target_properties(OnyxCli PROPERTIES CXX_SCAN_FOR_MODULES OFF)
