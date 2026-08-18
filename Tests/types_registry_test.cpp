// ── TypeCatalog / TypeRegistry tests (doctest) ────────────────────────────
//
// The registry's lazy index is the subtle part: handlers self-register during
// static init, but the TypeId handles they report are app-owned and stay 0
// until the app seeds the catalog inside main(). Indexing at registration time
// filed every handler under id 0 (each overwriting the last) and Resolve()
// never found anything -- the v0.5.2 bug. These tests pin that behaviour down.

#include <doctest/doctest.h>

#include <Onyx/Domain/MediaKind.h>
#include <Onyx/Types/ITypeHandler.h>
#include <Onyx/Types/TypeCatalog.h>
#include <Onyx/Types/TypeRegistry.h>

#include <memory>
#include <string>

using namespace Onyx::Types;
using Onyx::Domain::MediaKind;

namespace {

// Reports whatever its slot currently holds -- the same shape a real handler
// has, where GetId() reads a TypeId the app mints later.
class SlotHandler : public ITypeHandler {
public:
    SlotHandler(const TypeId* slot, const char* name) : m_slot(slot), m_name(name) {}
    TypeId GetId() const override { return *m_slot; }
    const char* GetName() const override { return m_name; }

private:
    const TypeId* m_slot;
    const char* m_name;
};

TypeInfo MakeInfo(const char* key, MediaKind media) {
    TypeInfo info;
    info.key   = key;
    info.label = std::string(key) + " label";
    info.media = media;
    return info;
}

} // namespace

TEST_CASE("TypeCatalog mints stable handles per key") {
    TypeCatalog& catalog = TypeCatalog::Get();

    const TypeId mesh = catalog.Register(MakeInfo("TEST_CATALOG_MESH", MediaKind::Mesh));
    const TypeId tex  = catalog.Register(MakeInfo("TEST_CATALOG_TEX", MediaKind::Image));

    CHECK(mesh.valid());
    CHECK(tex.valid());
    CHECK(mesh != tex);

    SUBCASE("re-registering a key returns the same handle") {
        const TypeId again = catalog.Register(MakeInfo("TEST_CATALOG_MESH", MediaKind::Mesh));
        CHECK(again == mesh);
    }

    SUBCASE("metadata round-trips") {
        CHECK(catalog.Media(mesh) == MediaKind::Mesh);
        CHECK(catalog.Media(tex) == MediaKind::Image);
        CHECK(std::string(catalog.Label(mesh)) == "TEST_CATALOG_MESH label");
        CHECK(catalog.Find("TEST_CATALOG_TEX") == tex);
    }

    SUBCASE("unknown keys and the null handle fall back to Unknown") {
        CHECK_FALSE(catalog.Find("TEST_CATALOG_NOPE").valid());
        CHECK(catalog.Media(TypeId{}) == MediaKind::Unknown);
    }
}

TEST_CASE("TypeRegistry indexes handlers lazily, after the catalog is seeded") {
    TypeRegistry& registry = TypeRegistry::Get();
    TypeCatalog& catalog   = TypeCatalog::Get();

    // Static-init order: the handlers register while their ids are still 0.
    // They keep pointing at these slots, exactly like a handler whose GetId()
    // reads an app-owned handle.
    static TypeId slotA, slotB, slotNever;

    registry.RegisterByTypeId(std::make_unique<SlotHandler>(&slotA, "lazyA"));
    registry.RegisterByTypeId(std::make_unique<SlotHandler>(&slotB, "lazyB"));
    registry.RegisterByTypeId(std::make_unique<SlotHandler>(&slotNever, "lazyNever"));

    // main() runs: the app seeds the catalog and the handles become real.
    slotA = catalog.Register(MakeInfo("TEST_LAZY_A", MediaKind::Mesh));
    slotB = catalog.Register(MakeInfo("TEST_LAZY_B", MediaKind::Audio));
    REQUIRE(slotA.valid());
    REQUIRE(slotB.valid());

    // Both resolve: neither was filed under -- and lost to -- id 0.
    ITypeHandler* a = registry.Resolve(slotA);
    ITypeHandler* b = registry.Resolve(slotB);
    REQUIRE(a != nullptr);
    REQUIRE(b != nullptr);
    CHECK(std::string(a->GetName()) == "lazyA");
    CHECK(std::string(b->GetName()) == "lazyB");

    // The handler still reporting 0 is skipped rather than shadowing Unknown.
    CHECK(registry.Resolve(TypeId{}) == nullptr);

    // A type nobody handles resolves to nullptr rather than to a stale entry.
    const TypeId orphan = catalog.Register(MakeInfo("TEST_LAZY_ORPHAN", MediaKind::Raw));
    CHECK(registry.Resolve(orphan) == nullptr);

    // The index is already built by now, so a late id change needs the explicit
    // invalidation hook.
    const TypeId moved = catalog.Register(MakeInfo("TEST_LAZY_A_MOVED", MediaKind::Mesh));
    slotA = moved;
    registry.InvalidateIndex();
    ITypeHandler* movedHandler = registry.Resolve(moved);
    REQUIRE(movedHandler != nullptr);
    CHECK(std::string(movedHandler->GetName()) == "lazyA");

    CHECK(registry.AllHandlers().size() >= 3);
}
