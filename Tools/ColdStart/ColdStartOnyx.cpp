// Cold-start compile check for <Onyx/Onyx.h> -- the "exam" T2's public-
// surface audit (docs/design/2026-08-19-public-surface-audit.md) proved by
// hand: a consumer who has cloned the repo, configured once (so
// build/_deps/*-src is populated the same way their own FetchContent
// would populate it), and points a compiler at ONLY -I Include plus the
// third-party dirs the umbrella needs, must be able to compile a TU that
// includes nothing but <Onyx/Onyx.h> -- no -I Source, no
// -I build/generated (Include/Onyx/Version.h is checked in since fbd54d3,
// so the umbrella's first include resolves without ever running CMake).
//
// This file is deliberately NOT a member of any CMakeLists.txt
// add_executable()/add_library() source list -- through-CMake compilation
// would put Source/ and build/generated on the include path via ordinary
// target properties, which defeats the point. It is compiled directly by
// a compiler invocation (see .github/workflows/ci.yml's "Cold-start header
// exam" step) that names its include dirs explicitly, one at a time, the
// same way T2/T8 proved this by hand with a throwaway TU outside the repo.
// Keeping the TU checked in (rather than authored fresh by CI every run)
// is what keeps this an exam that can be rerun locally, not a step that
// only ever existed inside a workflow log.
//
// Deliberately compile-only (cl /c), not link -- matching the audit's own
// "Variant A"/T8's re-verification method. It proves the header closure
// is self-sufficient; T6's Onyx::CliRender link-completeness proof is a
// separate, already-covered gate (OnyxCliRender/OnyxCli tests).

#include <Onyx/Onyx.h>

int main() {
    return 0;
}
