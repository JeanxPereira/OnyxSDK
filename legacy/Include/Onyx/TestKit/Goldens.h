#pragma once
// ── TestKit::Goldens (spec §10) ────────────────────────────────────────────
//
// "Tree goldens -- snapshot an AssetTree (names, keys, sizes, payload
// hashes) to JSON; diff on regression." SnapshotTree walks a parsed
// Modules::Document's tree (Domain::AssetEntry, the same structure the
// generic CLI's `list --json` already walks -- see Source/Cli/Commands.cpp's
// WriteEntryJson/PrintTree for the established recursive-walk pattern this
// follows) and renders it as byte-stable JSON: same hand-built-string
// approach Tools/OnyxOracle/RenderReport.h's BuildReport already uses and
// this repo already trusts for golden-file equality (two-space indent, keys
// in a fixed order, LF line endings, no timestamps/paths/pointers -- so
// identical inputs always produce an identical string, and a real
// regression is the only thing that ever changes the output).
//
// Every entry's payload hash is computed here (FNV-1a over the entry's
// declared ByteRange, read through the Document's file table) rather than
// trusted from AssetEntry::hash -- no module in this repo populates that
// field today, so relying on it would make every snapshot hash 0.

#include <Onyx/Modules/Workspace.h>

#include <filesystem>
#include <string>

namespace Onyx::TestKit {

/// Canonical JSON snapshot of `doc`'s parsed tree: for each entry (in tree
/// order, depth-first, exactly as stored) -- name, type key
/// (Types::TypeCatalog::Get().KeyOf), declared payload size, an FNV-1a hash
/// of the payload bytes themselves (0 for an entry with an empty ByteRange,
/// e.g. a pure branch/container node), the Failed flag, and its children
/// array recursively. Two-space indent, LF endings, no trailing whitespace.
/// Reading a payload's bytes never throws: a fileIndex out of range for
/// `doc`'s file table, or a short read, hashes whatever was actually read
/// (possibly zero bytes) rather than aborting the snapshot.
std::string SnapshotTree(const Modules::Document& doc);

/// True iff `snapshot` is byte-identical to goldenFile's contents. On any
/// mismatch (including a goldenFile that does not exist yet), `diffOut` is
/// filled with a description of the first differing line and `snapshot` is
/// written to a sibling file named `goldenFile` + ".actual" -- the same
/// "write what actually came out, next to the frozen expectation" pattern
/// the oracle's own render-corpus comparison already relies on, so a
/// developer can `diff` the two files directly to see the regression.
bool CompareTreeGolden(const std::string& snapshot, const std::filesystem::path& goldenFile,
                        std::string& diffOut);

} // namespace Onyx::TestKit
