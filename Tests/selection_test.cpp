#include <doctest/doctest.h>

#include <Onyx/Modules/Selection.h>

using namespace Onyx::Modules;

namespace {

// Fills `doc.roots` with a small nested tree, no file/parse involved:
//
//   roots[0] "root0"
//     children[0] "child0"
//     children[1] "child1"
//       children[0] "grandchild"
//   roots[1] "root1"
//
// Takes the Document by reference rather than returning one by value:
// Document holds a DiagSink, which owns a std::mutex, so Document is
// neither copyable nor movable -- there is nothing to return.
void FillNestedTree(Document& doc) {
    Onyx::Domain::AssetEntry grandchild;
    grandchild.name = "grandchild";

    Onyx::Domain::AssetEntry child1;
    child1.name = "child1";
    child1.children = {grandchild};

    Onyx::Domain::AssetEntry child0;
    child0.name = "child0";

    Onyx::Domain::AssetEntry root0;
    root0.name = "root0";
    root0.children = {child0, child1};

    Onyx::Domain::AssetEntry root1;
    root1.name = "root1";

    doc.roots = {root0, root1};
}

} // namespace

TEST_CASE("Selection: a valid deep path resolves to the right entry") {
    Document doc;
    FillNestedTree(doc);

    // roots[0].children[1].children[0] == "grandchild"
    NodePath path{{0, 1, 0}};
    const Onyx::Domain::AssetEntry* e = Resolve(doc, path);
    REQUIRE(e != nullptr);
    CHECK(e->name == "grandchild");

    // roots[1] == "root1" (single-hop path)
    NodePath rootPath{{1}};
    const Onyx::Domain::AssetEntry* r = Resolve(doc, rootPath);
    REQUIRE(r != nullptr);
    CHECK(r->name == "root1");
}

TEST_CASE("Selection: an out-of-range index at any depth resolves to nullptr") {
    Document doc;
    FillNestedTree(doc);

    // Out of range at the very first hop (doc.roots.size() == 2).
    CHECK(Resolve(doc, NodePath{{5}}) == nullptr);

    // Out of range at the second hop (root0.children.size() == 2).
    CHECK(Resolve(doc, NodePath{{0, 5}}) == nullptr);

    // Out of range at the third hop (child1.children.size() == 1).
    CHECK(Resolve(doc, NodePath{{0, 1, 5}}) == nullptr);

    // root1 has no children at all -- any further hop is out of range.
    CHECK(Resolve(doc, NodePath{{1, 0}}) == nullptr);
}

TEST_CASE("Selection: an empty path resolves to nullptr") {
    Document doc;
    FillNestedTree(doc);
    CHECK(Resolve(doc, NodePath{}) == nullptr);
    CHECK(Resolve(doc, NodePath{{}}) == nullptr);
}

TEST_CASE("Selection: a path built against a different, smaller tree resolves to nullptr") {
    Document big;
    FillNestedTree(big);
    NodePath deepPath{{0, 1, 0}}; // valid against `big`
    REQUIRE(Resolve(big, deepPath) != nullptr);

    // A smaller document -- one root, no children -- stands in for the
    // same document after being closed and reopened (or reparsed) into a
    // shallower tree. The old path must not resolve against it.
    Document small;
    Onyx::Domain::AssetEntry onlyRoot;
    onlyRoot.name = "solo";
    small.roots = {onlyRoot};

    CHECK(Resolve(small, deepPath) == nullptr);

    // Also stale against a document with no roots at all (e.g. a fresh,
    // not-yet-parsed Document).
    Document empty;
    CHECK(Resolve(empty, deepPath) == nullptr);
}
