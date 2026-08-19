# ── ReproTest.cmake — OracleReproducible gate ──────────────────────────────
#
# Runs `onyx-oracle render-corpus` twice, into WORK/a and WORK/b, then
# `onyx-oracle verify WORK/a WORK/b` to confirm the two runs are byte-
# identical. Any exit 77 (no display session -- see HeadlessGL.h) from any
# of the three invocations propagates as this script's own exit 77, via
# cmake_language(EXIT), so ctest's SKIP_RETURN_CODE 77 on the OracleReproducible
# test (set in CMakeLists.txt) turns the whole gate into a SKIP, not a FAIL,
# on machines with no GL display. Any other nonzero exit is a real failure
# and becomes a FATAL_ERROR (ctest FAILED).
#
# ORACLE and WORK are passed in via -D from the add_test COMMAND in
# CMakeLists.txt -- $<TARGET_FILE:onyx-oracle> only resolves inside an
# add_test COMMAND generator-expression context, not here, so the resolved
# path is threaded through as a cache variable instead.

if(NOT DEFINED ORACLE)
    message(FATAL_ERROR "ReproTest.cmake: ORACLE not set (pass -DORACLE=<path>)")
endif()
if(NOT DEFINED WORK)
    message(FATAL_ERROR "ReproTest.cmake: WORK not set (pass -DWORK=<dir>)")
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
        message(STATUS "onyx-oracle exited 77 (no GL display session) -- skipping")
        cmake_language(EXIT 77)
    elseif(NOT rc EQUAL 0)
        message(FATAL_ERROR "onyx-oracle ${ARGN} failed: exit ${rc}")
    endif()
endfunction()

run_oracle(render-corpus --out ${WORK}/a)
run_oracle(render-corpus --out ${WORK}/b)
run_oracle(verify ${WORK}/a ${WORK}/b)

message(STATUS "OracleReproducible: two independent render-corpus runs are byte-identical")
