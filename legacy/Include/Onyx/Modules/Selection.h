#pragma once

// Positional selection paths over a Document's asset tree (M3b Task 3).
//
// A NodePath addresses one AssetEntry by the sequence of child indices
// from a document's roots down to that node: indices[0] indexes
// Document::roots, and every subsequent index walks into the previous
// entry's .children. A path is pure data -- no pointer, no iterator --
// so it is safe to hold across frames even while the document it names
// keeps being reparsed or closed out from under it (spec §7.4: id-safe
// selection). Resolve() is what tells a holder whether the path still
// points at anything.
//
// Positional addressing is deliberately the answer to duplicate entry
// names within a document: two sibling AssetEntry values can share a
// `name` (or `displayName`), but no two siblings ever share a child
// index, so the path that reached one can never resolve to the other.
// There is no separate "uniquify by name" story needed on top of this.

#include <cstdint>
#include <vector>

#include <Onyx/Domain/Entry.h>
#include <Onyx/Modules/Workspace.h>

namespace Onyx::Modules {

struct NodePath {
    std::vector<uint32_t> indices;
};

// Event payload for "the user selected this node" (id-safe: carries a
// DocumentId + NodePath, never a raw AssetEntry* -- spec §7.4). Posted on
// the owning Workspace's EventBus by whoever draws the selection UI (the
// Shell's document browser, M3b Task 3).
struct SelectionChanged {
    DocumentId doc;
    NodePath   path;
};

// Resolves `path` against `doc`'s roots. Returns nullptr when:
//   - `path` is empty (an empty path never names a node), or
//   - any hop is out of range for the tree it is walking -- including
//     the very first hop into doc.roots.
// This covers the "stale path" case (the document was closed or
// reparsed to a smaller tree since the path was captured) and the
// "path built against a different tree" case identically: both just
// fail a bounds check somewhere along the walk.
const Domain::AssetEntry* Resolve(const Document& doc, const NodePath& path);

} // namespace Onyx::Modules
