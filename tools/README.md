# tools

`check.py` is the single build verdict. It runs configure, build, ctest and lint in order, prints one `ok`/`FAIL` line per step with elapsed seconds, stops at the first failing step and exits with that step's exit code. Flags: `--preset P` (default `windows-debug` on Windows, `linux-debug` elsewhere), `--no-build`, `--no-test`, `--lint-only`, `--ctest-regex R`. A ctest run that finds no tests is a failure. Release presets have no test preset, so ctest runs with `--test-dir build/<preset>`.

On Windows MSVC is not on `PATH`, so `check.py` locates the newest Visual Studio with the C++ workload through `vswhere.exe`, writes a temporary `.bat` that calls `vcvars64.bat`, and runs every cmake and ctest command inside it. On other systems the commands run directly.

`lint.py [root]` checks layer hygiene and prints `path:line: rule: detail`, exiting 1 on any hit. Rules: forbidden includes per layer, singleton accessors (`static T& Get()`/`Instance()`), game names under `Source/`, `Examples/` and `Tests/` (except `Tests/Golden/`), public headers without `#pragma once`, and project-history wording in comments under `Source/`. `legacy/`, `third_party/`, `build/` and `.superpowers/` are skipped. `lint_test.py` is its self-test and prints `ok 5`.
