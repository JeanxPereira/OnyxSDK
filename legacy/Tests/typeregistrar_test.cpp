#include <doctest/doctest.h>
#include <Onyx/Types/TypeCatalog.h>
#include <Onyx/Types/TypeRegistrar.h>

using namespace Onyx::Types;

TEST_CASE("TypeRegistrar mints keys inside its module namespace") {
    TypeCatalog& cat = TypeCatalog::Get();
    TypeRegistrar reg(cat, "m2test");

    TypeInfo mesh; mesh.key = "mesh"; mesh.label = "Mesh";
    TypeId id = reg.Add(mesh);
    CHECK(id.valid());
    CHECK(std::string(cat.KeyOf(id)) == "m2test.mesh");
    CHECK(cat.Find("m2test.mesh") == id);

    SUBCASE("re-adding the same bare key returns the same handle") {
        CHECK(reg.Add(mesh) == id);
    }
    SUBCASE("a dotted key is refused") {
        TypeInfo bad; bad.key = "other.mesh"; bad.label = "X";
        CHECK_FALSE(reg.Add(bad).valid());
    }
    SUBCASE("two registrars cannot collide") {
        TypeRegistrar reg2(cat, "m2other");
        TypeInfo alsoMesh; alsoMesh.key = "mesh"; alsoMesh.label = "Mesh";
        TypeId id2 = reg2.Add(alsoMesh);
        CHECK(id2.valid());
        CHECK(id2 != id);
        CHECK(std::string(cat.KeyOf(id2)) == "m2other.mesh");
    }
}

TEST_CASE("TypeCatalog::KeyOf answers for registered and invalid ids") {
    TypeCatalog& cat = TypeCatalog::Get();
    CHECK(std::string(cat.KeyOf(TypeId{})) == "");           // index 0 (Unknown) has no key
    CHECK(std::string(cat.KeyOf(TypeId{60000})) == "");     // out of range
}
