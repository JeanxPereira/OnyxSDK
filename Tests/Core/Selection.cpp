#include <doctest/doctest.h>

#include <Onyx/Core/Modules/Selection.h>

using namespace Onyx;
using namespace Onyx::Modules;

namespace {

Domain::AssetEntry Named(std::string name, std::vector<Domain::AssetEntry> children = {}) {
    Domain::AssetEntry e;
    e.name = std::move(name);
    e.children = std::move(children);
    return e;
}

void FillNestedTree(Document& doc) {
    doc.roots = {Named("root0", {Named("child0"), Named("child1", {Named("grandchild")})}), Named("root1")};
}

}

TEST_CASE("Selection.ValidDeepPathResolves") {
    Document doc;
    FillNestedTree(doc);

    const auto* deep = Resolve(doc, NodePath{{0, 1, 0}});
    REQUIRE(deep != nullptr);
    CHECK(deep->name == "grandchild");

    const auto* root = Resolve(doc, NodePath{{1}});
    REQUIRE(root != nullptr);
    CHECK(root->name == "root1");
}

TEST_CASE("Selection.OutOfRangeIndexResolvesToNull") {
    Document doc;
    FillNestedTree(doc);
    CHECK(Resolve(doc, NodePath{{5}}) == nullptr);
    CHECK(Resolve(doc, NodePath{{0, 5}}) == nullptr);
    CHECK(Resolve(doc, NodePath{{0, 1, 5}}) == nullptr);
    CHECK(Resolve(doc, NodePath{{1, 0}}) == nullptr);
}

TEST_CASE("Selection.EmptyPathResolvesToNull") {
    Document doc;
    FillNestedTree(doc);
    CHECK(Resolve(doc, NodePath{}) == nullptr);
}

TEST_CASE("Selection.StalePathAgainstASmallerTreeResolvesToNull") {
    Document big;
    FillNestedTree(big);
    const NodePath deep{{0, 1, 0}};
    REQUIRE(Resolve(big, deep) != nullptr);

    Document small;
    small.roots = {Named("solo")};
    CHECK(Resolve(small, deep) == nullptr);

    Document empty;
    CHECK(Resolve(empty, deep) == nullptr);
}

TEST_CASE("Selection.FindByNameReturnsTheFirstDepthFirstMatch") {
    Document doc;
    doc.roots = {Named("a", {Named("dup", {Named("inner")}), Named("b")}), Named("dup"), Named("inner")};

    const auto first = FindByName(doc, "dup");
    REQUIRE(first.has_value());
    CHECK(first->indices == std::vector<uint32_t>{0, 0});

    const auto inner = FindByName(doc, "inner");
    REQUIRE(inner.has_value());
    CHECK(inner->indices == std::vector<uint32_t>{0, 0, 0});

    const auto root = FindByName(doc, "a");
    REQUIRE(root.has_value());
    CHECK(root->indices == std::vector<uint32_t>{0});

    CHECK_FALSE(FindByName(doc, "missing").has_value());
    CHECK(Resolve(doc, *first)->name == "dup");
}
