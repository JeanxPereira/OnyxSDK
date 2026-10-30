# Compiles every public header of a layer as the only include of its own
# translation unit, so a header that leans on a missing include or on an upper
# layer fails the build. The OBJECT library links Onyx::<Layer>, which gives it
# exactly the consumer's view: the layer's own include dir, the layers below
# and their third-party dependencies, and nothing from the layers above.
function(onyx_add_standalone_headers layer)
    if(TARGET Onyx${layer}Standalone)
        return()
    endif()
    set(include_root "${PROJECT_SOURCE_DIR}/Source/${layer}/include")
    file(GLOB_RECURSE headers CONFIGURE_DEPENDS
        RELATIVE "${include_root}"
        "${include_root}/Onyx/${layer}/*.h")

    set(out_dir "${PROJECT_BINARY_DIR}/generated/standalone/${layer}")
    set(generated "")
    set(index 0)
    foreach(header IN LISTS headers)
        string(REPLACE "/" "_" stem "${header}")
        set(cpp "${out_dir}/${stem}.cpp")
        file(GENERATE OUTPUT "${cpp}" CONTENT
"#include <${header}>
int OnyxStandalone_${layer}_${index}() { return 0; }
")
        list(APPEND generated "${cpp}")
        math(EXPR index "${index} + 1")
    endforeach()

    if(NOT generated)
        return()
    endif()
    add_library(Onyx${layer}Standalone OBJECT ${generated})
    target_link_libraries(Onyx${layer}Standalone PRIVATE Onyx::${layer})
    onyx_warnings(Onyx${layer}Standalone)
    set_target_properties(Onyx${layer}Standalone PROPERTIES CXX_SCAN_FOR_MODULES OFF)
endfunction()
