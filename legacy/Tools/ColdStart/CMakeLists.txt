# -- ColdStart -- the umbrella's compile-AND-LINK exam -----------------------
# The consumer contract README's "Consuming Onyx" states, executed:
#
#     target_link_libraries(MyApp PRIVATE Onyx::Onyx)
#     #include <Onyx/Onyx.h>
#
# This target links `Onyx::Onyx` and NOTHING else -- no Onyx_Core, no
# Onyx_Render, no Onyx_Exchange/CliRender/TestKit, no third-party target of
# its own, no include directory of its own. Anything <Onyx/Onyx.h> declares
# that does not ship in Onyx_Core/Onyx_Render/Onyx_Shell fails HERE, at link,
# which is the whole point: the umbrella's own inclusion rule (clause 1,
# Include/Onyx/Onyx.h) is now enforced by the build rather than by argument.
#
# Why this exists at all: through v0.6.0 the cold-start exam was compile-only
# (`cl /c`, see .github/workflows/ci.yml), and a compile-only exam cannot see
# an unresolved external. The umbrella shipped declarations from three
# targets Onyx::Onyx does not link for an entire milestone, through four
# review rounds, because every round asked "does it compile".
#
# The raw `cl /c` step in ci.yml stays as well and is NOT redundant: it
# proves the header closure needs no -I Source and no -I build/generated,
# which a CMake target (which inherits Onyx::Onyx's PUBLIC include dirs)
# cannot prove. Compile-without-CMake and link-through-CMake are two
# different claims and the exam makes both.
#
# Unconditional, like Tools/OnyxOracle: the `ColdStart` ctest entry is part
# of the standard suite, not example- or test-option-gated. It is headless
# and touches no GPU (the window/Vulkan boot path in ColdStartOnyx.cpp is
# linked but gated behind `--boot`, which this test never passes), so it has
# no SKIP_RETURN_CODE and must pass on every runner.
add_executable(ColdStartToolkit ${CMAKE_CURRENT_SOURCE_DIR}/ColdStartOnyx.cpp)
target_link_libraries(ColdStartToolkit PRIVATE Onyx::Onyx)

add_test(NAME ColdStart COMMAND ColdStartToolkit)
set_tests_properties(ColdStart PROPERTIES LABELS "surface")
