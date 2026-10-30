function(onyx_add_layer name)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPS;PUBLIC_DEPS" ${ARGN})

    file(GLOB_RECURSE layer_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
    add_library(Onyx_${name} STATIC ${layer_sources} ${ARG_SOURCES})
    add_library(Onyx::${name} ALIAS Onyx_${name})

    target_compile_features(Onyx_${name} PUBLIC cxx_std_20)
    target_include_directories(Onyx_${name} PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
    if(ARG_PUBLIC_DEPS)
        target_link_libraries(Onyx_${name} PUBLIC ${ARG_PUBLIC_DEPS})
    endif()
    if(ARG_DEPS)
        target_link_libraries(Onyx_${name} PRIVATE ${ARG_DEPS})
    endif()
    onyx_warnings(Onyx_${name})

    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Pch.h")
        target_precompile_headers(Onyx_${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/Pch.h")
    endif()
endfunction()
