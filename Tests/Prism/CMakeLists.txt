# onyx_add_tests links Onyx::<Layer>. Prism is an example module whose library is defined after
# Tests/, so this name forwards to it by target name and resolves at generate time.
add_library(Onyx::Prism INTERFACE IMPORTED)
target_link_libraries(Onyx::Prism INTERFACE Prism)
onyx_add_tests(Prism LINK Onyx::ExamplePrism Onyx::TestKit)
