add_test(NAME Consumer.CoreOnly
    COMMAND ${CMAKE_COMMAND}
        -D ONYX_ROOT=${PROJECT_SOURCE_DIR}
        -D CONSUMER_SRC=${CMAKE_CURRENT_SOURCE_DIR}/CoreOnly
        -D WORK=${CMAKE_BINARY_DIR}/consumer-coreonly
        -D GENERATOR=${CMAKE_GENERATOR}
        -D BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -D C_COMPILER=${CMAKE_C_COMPILER}
        -D CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/ExternalConsumption.cmake)
set_tests_properties(Consumer.CoreOnly PROPERTIES TIMEOUT 900)
