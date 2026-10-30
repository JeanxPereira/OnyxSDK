# ── RenderStrictTest.cmake -- OnyxCliRenderStrict gate (M5 Task 6) ─────────
#
# Same shape as RenderTest.cmake (its own top comment explains the 77-
# propagation/SKIP mechanics -- not repeated here).
#
# What this asserts, using `write-render-strict-fixture` (Examples/OnyxCli/
# Main.cpp's own top comment explains the fixture's shape: a good "cube"
# mesh entry plus a "bad" entry with an out-of-bounds payload range, which
# OnyxBoxModule's TOC walk reports as a document-level Error diag
# regardless of which entry is later decoded):
#   1. `render <fixture> cube --out <path>` (no --strict) exits 0 -- the
#      Error diag belongs to the SIBLING "bad" entry, not "cube" itself, so
#      the render of "cube" succeeds on its own merits; a PNG is written.
#   2. `render <fixture> cube --out <path> --strict` on the SAME fixture
#      exits kStrictErrors (3, Include/Onyx/Cli/Commands.h) -- proving
#      --strict is what changes the exit code, not the fixture failing to
#      render -- and STILL writes a PNG (CmdRender's own doc comment: the
#      render completes regardless of --strict, only the exit code
#      changes).

if(NOT DEFINED CLI)
    message(FATAL_ERROR "RenderStrictTest.cmake: CLI not set (pass -DCLI=<path>)")
endif()
if(NOT DEFINED WORK)
    message(FATAL_ERROR "RenderStrictTest.cmake: WORK not set (pass -DWORK=<dir>)")
endif()

file(REMOVE_RECURSE ${WORK})
file(MAKE_DIRECTORY ${WORK})
set(FIXTURE ${WORK}/render-strict-fixture.obx)

execute_process(
    COMMAND ${CLI} write-render-strict-fixture ${FIXTURE}
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE errout)
if(out)
    message(STATUS "${out}")
endif()
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "write-render-strict-fixture failed: exit ${rc}\n${errout}")
endif()

# Step 1: without --strict, the Error diag from the sibling "bad" entry
# does not affect "cube"'s own render.
set(NONSTRICT_PNG ${WORK}/nonstrict.png)
execute_process(
    COMMAND ${CLI} render ${FIXTURE} cube --out ${NONSTRICT_PNG} --width 64 --height 64
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
    message(FATAL_ERROR "render (no --strict) on the strict fixture failed: exit ${rc} -- expected 0")
endif()
if(NOT EXISTS ${NONSTRICT_PNG})
    message(FATAL_ERROR "render (no --strict) exited 0 but did not write ${NONSTRICT_PNG}")
endif()

# Step 2: --strict on the identical fixture/entry turns the same Error
# diag into a nonzero exit -- kStrictErrors == 3 (Include/Onyx/Cli/
# Commands.h; kept as a literal here since this script has no access to
# the C++ header's constant).
set(STRICT_PNG ${WORK}/strict.png)
execute_process(
    COMMAND ${CLI} render ${FIXTURE} cube --out ${STRICT_PNG} --width 64 --height 64 --strict
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE errout)
if(out)
    message(STATUS "${out}")
endif()
if(errout)
    message(STATUS "${errout}")
endif()
if(NOT rc EQUAL 3)
    message(FATAL_ERROR "render --strict on the strict fixture exited ${rc}, expected 3 (kStrictErrors)")
endif()
if(NOT EXISTS ${STRICT_PNG})
    message(FATAL_ERROR "render --strict exited 3 but did not write ${STRICT_PNG} -- the render itself "
                         "must still complete; only the exit code should change")
endif()

message(STATUS "OnyxCliRenderStrict: render exits 0 without --strict and 3 (kStrictErrors) with "
                "--strict on the same Error-diag-carrying fixture, writing a PNG either way")
