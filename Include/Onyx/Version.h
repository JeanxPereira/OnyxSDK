#pragma once

// Generated from cmake/Version.h.in -- the version lives in the top-level
// project() call, so CMake and C++ can never disagree about it.
//
// Written directly to Include/Onyx/Version.h (checked into git, not a
// build/generated/ path) so <Onyx/Onyx.h>'s first include resolves for a
// consumer who has only cloned the repository and never run CMake -- see
// this file's own configure_file() call in the root CMakeLists.txt.

#define ONYX_VERSION_MAJOR 1
#define ONYX_VERSION_MINOR 1
#define ONYX_VERSION_PATCH 0
#define ONYX_VERSION_STRING "1.1.0"

namespace Onyx {

// e.g. "1.0.0" -- handy for About boxes and bug reports.
inline constexpr const char* Version() { return ONYX_VERSION_STRING; }

} // namespace Onyx
