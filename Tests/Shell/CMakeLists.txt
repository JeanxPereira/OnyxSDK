include(${PROJECT_SOURCE_DIR}/cmake/Assets.cmake)
onyx_add_tests(Shell LINK Onyx::TestKit Onyx::ExampleBox)
onyx_add_tests(Shell SUITE Window SKIP_RETURN_CODE 77 LINK Onyx::TestKit Onyx::ExampleBox)
onyx_install_assets(OnyxShellWindowTests)
