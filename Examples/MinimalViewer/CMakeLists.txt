include(${PROJECT_SOURCE_DIR}/cmake/Assets.cmake)

add_executable(MinimalViewer Main.cpp)
target_link_libraries(MinimalViewer PRIVATE Onyx::Shell Onyx::ExampleBox)
onyx_warnings(MinimalViewer)
set_target_properties(MinimalViewer PROPERTIES CXX_SCAN_FOR_MODULES OFF)
onyx_install_assets(MinimalViewer)
