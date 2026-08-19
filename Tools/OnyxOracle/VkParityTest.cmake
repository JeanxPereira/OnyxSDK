# ── VkParityTest.cmake — VkOracleParity gate (task 7, "the milestone's
#    teeth") ──────────────────────────────────────────────────────────────
#
# Renders the 5-scene corpus once through the Vulkan path
# (`render-corpus --renderer vk`) into WORK, then `compare`s it against the
# frozen GL goldens (Tests/Golden/corpus) within the four-knob tolerance
# CMakeLists.txt passes in (T7 fix round, adjudicated) -- see that file's
# VkOracleParity comment for the tuned values and the diagnosis that
# justified them, and task-7-report.md for the full tuning log and final
# per-scene metrics.
#
# Any exit 77 (no Vulkan-capable device/driver -- see VkContext::Init) from
# render-corpus propagates as this script's own exit 77, via
# cmake_language(EXIT), so ctest's SKIP_RETURN_CODE 77 on VkOracleParity
# turns the whole gate into a SKIP, not a FAIL, on a machine with no usable
# GPU. Any other nonzero exit (including `compare` reporting a tolerance
# failure) is a real failure and becomes a FATAL_ERROR (ctest FAILED).
#
# ORACLE/WORK/GOLDEN/MAX_CHANNEL_DELTA/MAX_DIFFERING_PCT/MAX_HIGH_DELTA_PCT/
# MAX_MAE are passed in via -D from the add_test COMMAND in CMakeLists.txt,
# same pattern ReproTest.cmake already uses ($<TARGET_FILE:onyx-oracle>
# only resolves inside an add_test COMMAND generator-expression context,
# not here).

if(NOT DEFINED ORACLE)
    message(FATAL_ERROR "VkParityTest.cmake: ORACLE not set (pass -DORACLE=<path>)")
endif()
if(NOT DEFINED WORK)
    message(FATAL_ERROR "VkParityTest.cmake: WORK not set (pass -DWORK=<dir>)")
endif()
if(NOT DEFINED GOLDEN)
    message(FATAL_ERROR "VkParityTest.cmake: GOLDEN not set (pass -DGOLDEN=<dir>)")
endif()
if(NOT DEFINED MAX_CHANNEL_DELTA)
    message(FATAL_ERROR "VkParityTest.cmake: MAX_CHANNEL_DELTA not set (pass -DMAX_CHANNEL_DELTA=<N>)")
endif()
if(NOT DEFINED MAX_DIFFERING_PCT)
    message(FATAL_ERROR "VkParityTest.cmake: MAX_DIFFERING_PCT not set (pass -DMAX_DIFFERING_PCT=<P>)")
endif()
if(NOT DEFINED MAX_HIGH_DELTA_PCT)
    message(FATAL_ERROR "VkParityTest.cmake: MAX_HIGH_DELTA_PCT not set (pass -DMAX_HIGH_DELTA_PCT=<P2>)")
endif()
if(NOT DEFINED MAX_MAE)
    message(FATAL_ERROR "VkParityTest.cmake: MAX_MAE not set (pass -DMAX_MAE=<M>)")
endif()

function(run_oracle)
    execute_process(
        COMMAND ${ORACLE} ${ARGN}
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
        message(STATUS "onyx-oracle exited 77 -- skipping")
        cmake_language(EXIT 77)
    elseif(NOT rc EQUAL 0)
        message(FATAL_ERROR "onyx-oracle ${ARGN} failed: exit ${rc}")
    endif()
endfunction()

run_oracle(render-corpus --renderer vk --out ${WORK})
# --emit-metrics: always on here so every ctest log carries the raw
# per-scene numbers (maxDelta/differingPct/highDeltaPct/mae) regardless of
# pass/fail -- the substrate a future ratchet mode (T12) reads from, and
# exactly what a human re-tuning these four cache variables on a new GPU
# needs to see without re-running compare by hand.
run_oracle(compare ${GOLDEN} ${WORK}
           --max-channel-delta ${MAX_CHANNEL_DELTA}
           --max-differing-pct ${MAX_DIFFERING_PCT}
           --max-high-delta-pct ${MAX_HIGH_DELTA_PCT}
           --max-mae ${MAX_MAE}
           --emit-metrics)

message(STATUS "VkOracleParity: Vulkan render-corpus matches Tests/Golden/corpus within tolerance "
               "(maxChannelDelta<=${MAX_CHANNEL_DELTA}, differingPct<=${MAX_DIFFERING_PCT}%, "
               "highDeltaPct<=${MAX_HIGH_DELTA_PCT}%, mae<=${MAX_MAE})")
