#pragma once

#include <Onyx/Core/Cli/Verb.h>
#include <Onyx/Core/Modules/Workspace.h>

#include <iosfwd>

namespace Onyx::Cli {

inline constexpr int kOk = 0;
inline constexpr int kUsage = 1;
inline constexpr int kNoModule = 2;
inline constexpr int kStrictErrors = 3;
inline constexpr int kPartial = 4;

// Dispatches argv[1] to its verb with the remaining arguments. With no verb or an unknown one it
// writes a usage listing (built from argv[0] and every verb, sorted) to `err` and returns kUsage.
// `argv` is UTF-8 on every platform; a verb builds paths with Services::Paths::FromUtf8.
int Run(Modules::Workspace& ws, const VerbTable& table, int argc, char** argv, std::ostream& out,
        std::ostream& err);

}
