#pragma once

#include <Onyx/Core/Modules/Document.h>
#include <Onyx/Core/Types/TypeCatalog.h>

#include <filesystem>
#include <string>
#include <string_view>

namespace Onyx::TestKit {

// Canonical JSON of a parsed tree, depth first in stored order: name, key (the catalog's full type
// key), kind (MediaKind name), size (the declared byte range size), flags (NodeFlags as a number)
// and children. Two-space indent, LF endings, a trailing newline, nothing that varies per run.
std::string SnapshotTree(const Modules::Document& doc, const Types::TypeCatalog& catalog);

// True when `snapshot` equals the golden file byte for byte. Otherwise `diffOut` names the first
// differing line (or the line counts, or the missing golden) and the snapshot is written next to
// the golden as "<golden>.actual".
bool CompareTreeGolden(std::string_view snapshot, const std::filesystem::path& goldenFile, std::string& diffOut);

// Overwrites the golden (creating parent directories) only when the environment variable
// ONYX_UPDATE_GOLDENS is "1". Returns whether it wrote.
bool UpdateGolden(std::string_view snapshot, const std::filesystem::path& goldenFile);

}
