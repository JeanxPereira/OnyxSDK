#include <doctest/doctest.h>

#include <Onyx/Container/ChunkSchema.h>

using Onyx::Container::ChunkSchema;

TEST_CASE("ChunkSchema records containers, leaves, and allowed children") {
    ChunkSchema s;
    s.SetContainer("ROOT", {"LEAA", "LEAB"});
    s.SetLeaf("LEAA");

    CHECK(s.IsContainer("ROOT"));
    CHECK_FALSE(s.IsContainer("LEAA"));   // a declared leaf
    CHECK_FALSE(s.IsContainer("ZZZZ"));   // unknown tag is not a container

    CHECK(s.Allows("ROOT", "LEAA"));
    CHECK(s.Allows("ROOT", "LEAB"));
    CHECK_FALSE(s.Allows("ROOT", "ZZZZ"));
    CHECK_FALSE(s.Allows("LEAA", "LEAB")); // a non-container allows nothing
}
