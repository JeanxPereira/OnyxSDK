# ── RenderViewsTest.cmake -- OnyxCliRenderViews gate (M5 Task 6) ───────────
#
# Same shape as RenderTest.cmake (its own top comment explains the 77-
# propagation/SKIP mechanics -- not repeated here): drive the real CLI
# binary through execute_process(), propagate a real `render` exit 77 (no
# Vulkan-capable device/driver) as this script's own exit 77 so ctest's
# SKIP_RETURN_CODE 77 on OnyxCliRenderViews turns the whole gate into a
# SKIP, not a FAIL, on a machine with no GPU.
#
# What this asserts: `render <fixture> cube --out <path> --views
# iso,front,back,left,right,top` (every name in Include/Onyx/Cli/
# Commands.h's kCanonicalViews, in that exact order) exits 0 and produces
# exactly one PNG per requested view, named by DerivePerViewPath's own rule
# (Source/Cli/Commands.cpp): `<path>` with the view name inserted before
# the extension. This is the brief's own "assert N PNGs" proof for
# `--views` -- N == 6 here, the full canonical set, not a subset, so a
# regression that drops or renames a view breaks this test rather than
# going unnoticed.

if(NOT DEFINED CLI)
    message(FATAL_ERROR "RenderViewsTest.cmake: CLI not set (pass -DCLI=<path>)")
endif()
if(NOT DEFINED WORK)
    message(FATAL_ERROR "RenderViewsTest.cmake: WORK not set (pass -DWORK=<dir>)")
endif()

file(REMOVE_RECURSE ${WORK})
file(MAKE_DIRECTORY ${WORK})
set(FIXTURE ${WORK}/render-fixture.obx)
set(OUTBASE ${WORK}/out.png)
set(VIEWS iso front back left right top)

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

list(JOIN VIEWS "," VIEWS_CSV)
execute_process(
    COMMAND ${CLI} render ${FIXTURE} cube --out ${OUTBASE} --width 64 --height 64 --views ${VIEWS_CSV}
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
    message(STATUS "render --views exited 77 (no Vulkan-capable device/driver) -- skipping")
    cmake_language(EXIT 77)
elseif(NOT rc EQUAL 0)
    message(FATAL_ERROR "render --views ${VIEWS_CSV} failed: exit ${rc}")
endif()

foreach(_v ${VIEWS})
    set(_expected ${WORK}/out.${_v}.png)
    if(NOT EXISTS ${_expected})
        message(FATAL_ERROR "render --views did not produce ${_expected}")
    endif()
endforeach()
if(EXISTS ${OUTBASE})
    message(FATAL_ERROR "render --views wrote ${OUTBASE} directly -- --out's own path must only be "
                         "used as a naming base when --views is given, never written to itself")
endif()

message(STATUS "OnyxCliRenderViews: render --views iso,front,back,left,right,top produced all 6 "
                "expected PNGs, none written to --out's own path")
