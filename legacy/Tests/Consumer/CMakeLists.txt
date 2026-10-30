# -- ExternalConsumption -- the FetchContent/add_subdirectory consumption
# gate --------------------------------------------------------------------
# README's "Consuming Onyx" section states, in bold, that source consumption
# via FetchContent/add_subdirectory is the ONE model v1 supports, and
# justifies shipping no install()/export() on the grounds that vendoring
# "demonstrably works today". Nothing in the v1.0.0 public-surface audit
# (Tools/ColdStart included) actually exercised that model -- ColdStart
# links Onyx::Onyx, but as part of Onyx's OWN top-level configure, so
# CMAKE_SOURCE_DIR there is Onyx's root by definition. Every consumer-facing
# path in this build that used to resolve through CMAKE_SOURCE_DIR instead
# of ONYX_ROOT_DIR (cmake/ShaderCompile.cmake, the two LayerGuard ctest
# entries, Tests/CMakeLists.txt's and Tools/OnyxOracle/CMakeLists.txt's own
# CMAKE_SOURCE_DIR uses) was invisible to every exam that ran with Onyx as
# the top-level project -- which was all of them.
#
# This test closes that gap: it configures and builds a standalone consumer
# project (consumer_project/) that add_subdirectory()s this Onyx checkout
# from OUTSIDE its own source tree, into a fresh out-of-tree build directory
# -- exactly the condition that made CMAKE_SOURCE_DIR resolve to the wrong
# root before the fix. See ExternalConsumptionTest.cmake for the mechanics
# and for the demonstration record.
#
# LABELS "surface", matching ColdStart -- both are public-surface exams, not
# render-output gates, so neither carries render's SKIP_RETURN_CODE 77
# convention. TIMEOUT is generous (this nested configure+build compiles
# Onyx_Core/Onyx_Render/Onyx_Shell from source a second time, into a build
# tree the outer build cannot share object files with -- only the
# FetchContent third-party sources are reused, via DEPS_CACHE below).
add_test(NAME ExternalConsumption
    COMMAND ${CMAKE_COMMAND}
        -DONYX_ROOT=${ONYX_ROOT_DIR}
        -DCONSUMER_SRC=${CMAKE_CURRENT_SOURCE_DIR}/consumer_project
        -DWORK=${CMAKE_CURRENT_BINARY_DIR}/consumer-build
        -DGENERATOR=${CMAKE_GENERATOR}
        -DBUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DDEPS_CACHE=${CMAKE_BINARY_DIR}/_deps
        -P ${CMAKE_CURRENT_SOURCE_DIR}/ExternalConsumptionTest.cmake)
set_tests_properties(ExternalConsumption PROPERTIES LABELS "surface" TIMEOUT 2400)
