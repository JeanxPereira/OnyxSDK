#pragma once

#include <Onyx/Cli/StockVerbs.h>
#include <Onyx/Core/Cli/Resolve.h>
#include <Onyx/Core/Domain/AssetEntry.h>
#include <Onyx/Core/Modules/Document.h>
#include <Onyx/Core/Modules/EntryAccess.h>
#include <Onyx/Core/Modules/Workspace.h>

#include <filesystem>
#include <iosfwd>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Onyx::Cli::Detail {

struct Args {
    std::vector<std::string_view> positional;
    bool json = false;
    bool strict = false;
    bool first = false;
    std::string_view game;
    std::string_view to;
    std::string_view out;
};

// Flags may appear anywhere after the verb. False for an unknown `--` token or a value flag with
// nothing after it.
bool ParseArgs(std::span<const std::string_view> args, Args& parsed);

// Names the failure on `out` and returns false when no module accepts the file.
bool OpenOrReport(const Modules::ScopedDocument& opened, const std::filesystem::path& path, std::ostream& out);

bool IsFailed(const Domain::AssetEntry& entry);

Verb ProbeVerb();
Verb ListVerb();
Verb ExtractVerb();
Verb DecodeVerb();

}
