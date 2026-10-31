onyx_add_tests(Ui LINK Onyx::TestKit Onyx::ExampleBox)
onyx_add_tests(Ui SUITE Gpu SKIP_RETURN_CODE 77 LINK Onyx::TestKit Onyx::ExampleBox)
