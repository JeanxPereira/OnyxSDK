# ── RenderTest.cmake -- OnyxCliRender gate (M4 Task 14) ────────────────────
#
# Mirrors Tools/OnyxOracle/ReproTest.cmake's shape exactly: drive the real
# CLI binary through execute_process(), propagate its own exit 77 (no
# Vulkan-capable device/driver -- Onyx::Rendering::VkContext::Init) as this
# script's exit 77 via cmake_language(EXIT), so ctest's SKIP_RETURN_CODE 77
# on OnyxCliRender (set in CMakeLists.txt) turns the whole gate into a SKIP,
# not a FAIL, on a machine with no GPU. Any other nonzero exit is a real
# failure and becomes a FATAL_ERROR (ctest FAILED).
#
# What this actually asserts, and why "non-uniform" is not re-checked here:
#   1. `write-render-fixture` produces a one-entry (kind=3 mesh, name
#      "cube") .obx file (Examples/OnyxCli/Main.cpp's WriteRenderFixture).
#   2. `render` is run TWICE, independently, into WORK/a.png and
#      WORK/b.png.
#   3. Both PNGs must exist (render exiting 0 already implies this --
#      CmdRender itself refuses to return kOk without a written PNG) and be
#      byte-identical (`cmake -E compare_files`).
#   4. "is non-uniform" (the task brief's third assertion) is enforced
#      INSIDE CmdRender itself (Source/Cli/Render.cpp): a render whose
#      readback is nothing but the clear color returns kUsage with a
#      diagnostic instead of writing a PNG. A pure-CMake pixel inspection
#      of the PNG would need a PNG decoder this script does not have --
#      pushing the check into the command under test is both simpler and
#      strictly stronger (every future caller of `render`, not just this
#      ctest, gets the same guarantee). So step 2 succeeding (exit 0, not
#      77, not any other nonzero code) is itself the "non-uniform" proof.
#
# CLI and WORK are passed in via -D from the add_test COMMAND in
# CMakeLists.txt -- $<TARGET_FILE:onyxbox-cli> only resolves inside an
# add_test COMMAND generator-expression context, not here, so the resolved
# path is threaded through as a cache variable instead (same reasoning
# ReproTest.cmake's own top comment gives for ORACLE).

if(NOT DEFINED CLI)
    message(FATAL_ERROR "RenderTest.cmake: CLI not set (pass -DCLI=<path>)")
endif()
if(NOT DEFINED WORK)
    message(FATAL_ERROR "RenderTest.cmake: WORK not set (pass -DWORK=<dir>)")
endif()

file(REMOVE_RECURSE ${WORK})
file(MAKE_DIRECTORY ${WORK})
set(FIXTURE ${WORK}/render-fixture.obx)

execute_process(
    COMMAND ${CLI} write-render-fixture ${FIXTURE}
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE errout)
if(out)
    message(STATUS "${out}")
endif()
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "write-render-fixture failed: exit ${rc}\n${errout}")
endif()

function(run_render outfile)
    execute_process(
        COMMAND ${CLI} render ${FIXTURE} cube --out ${outfile} --width 64 --height 64
        RESULT_VARIABLE rc
        OUTPUT_VARIABLE out
        ERROR_VARIABLE errout)
    if(out)
        message(STATUS "${out}")
    endif()
    if(errout)
        message(STATUS "${errout}")
    endif()
    if(rc EQUAL 77)
        message(STATUS "render exited 77 (no Vulkan-capable device/driver) -- skipping")
        cmake_language(EXIT 77)
    elseif(NOT rc EQUAL 0)
        message(FATAL_ERROR "render ${outfile} failed: exit ${rc}")
    endif()
endfunction()

run_render(${WORK}/a.png)
run_render(${WORK}/b.png)

if(NOT EXISTS ${WORK}/a.png OR NOT EXISTS ${WORK}/b.png)
    message(FATAL_ERROR "render exited 0 but did not produce both PNGs")
endif()

execute_process(
    COMMAND ${CMAKE_COMMAND} -E compare_files ${WORK}/a.png ${WORK}/b.png
    RESULT_VARIABLE cmp)
if(NOT cmp EQUAL 0)
    message(FATAL_ERROR "two independent render runs did not produce byte-identical PNGs")
endif()

message(STATUS "OnyxCliRender: two independent render runs produced existing, "
                "byte-identical, non-uniform (see this file's top comment) PNGs")
