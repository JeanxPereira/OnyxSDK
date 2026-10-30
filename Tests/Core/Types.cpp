#include <doctest/doctest.h>

#include <Onyx/Core/Domain/MediaKind.h>
#include <Onyx/Core/Types/TypeCatalog.h>
#include <Onyx/Core/Types/TypeRegistrar.h>

#include <ostream>
#include <set>
#include <string>
#include <string_view>
#include <thread>

using namespace Onyx;
using namespace Onyx::Types;

TEST_CASE("Types.CatalogMintsStableHandlesPerKey") {
    TypeCatalog catalog;
    TypeSpec a;
    a.key = "a";
    a.label = "A";
    TypeSpec b;
    b.key = "b";
    b.label = "B";
    const TypeId ia = catalog.Register("m", a);
    const TypeId ib = catalog.Register("m", b);
    CHECK(ia.Valid());
    CHECK(ib.Valid());
    CHECK(ia != ib);
    CHECK(catalog.Find("m.a") == ia);
    CHECK(catalog.Find("m.b") == ib);
    CHECK(catalog.Key(ia) == "m.a");
    CHECK(catalog.Spec(ia).label == "A");
    CHECK_FALSE(catalog.Find("m.missing").Valid());
}

TEST_CASE("Types.RegisterExistingKeyReturnsSameId") {
    TypeCatalog catalog;
    TypeSpec first;
    first.key = "k";
    first.label = "First";
    first.media = Domain::MediaKind::Mesh;
    TypeSpec second = first;
    second.label = "Second";
    second.media = Domain::MediaKind::Audio;
    const TypeId a = catalog.Register("m", first);
    const size_t count = catalog.Count();
    const TypeId b = catalog.Register("m", second);
    CHECK(a == b);
    CHECK(catalog.Count() == count);
    CHECK(catalog.Spec(a).label == "First");
    CHECK(catalog.Media(a) == Domain::MediaKind::Mesh);
}

TEST_CASE("Types.RegistrarPrefixesModuleId") {
    TypeCatalog catalog;
    TypeRegistrar registrar(catalog, "obx");
    TypeSpec spec;
    spec.key = "mesh";
    const TypeId id = registrar.Add(spec);
    CHECK(id.Valid());
    CHECK(catalog.Key(id) == "obx.mesh");
    CHECK(registrar.ModuleId() == "obx");
    CHECK(catalog.Find("obx.mesh") == id);
}

TEST_CASE("Types.RegistrarRejectsDottedKey") {
    TypeCatalog catalog;
    TypeRegistrar registrar(catalog, "obx");
    const size_t before = catalog.Count();
    TypeSpec spec;
    spec.key = "a.b";
    const TypeId id = registrar.Add(spec);
    CHECK_FALSE(id.Valid());
    CHECK(catalog.Count() == before);
    CHECK_FALSE(catalog.Find("obx.a.b").Valid());
}

TEST_CASE("Types.BuiltinsExist") {
    TypeCatalog catalog;
    CHECK(catalog.Find("onyx.unknown").value == 0);
    CHECK(catalog.Find("onyx.folder").Valid());
    CHECK(catalog.Find("onyx.raw").Valid());
    CHECK(catalog.Count() == 3);
    CHECK(catalog.Media(catalog.Find("onyx.folder")) == Domain::MediaKind::Container);
    CHECK(catalog.Media(catalog.Find("onyx.raw")) == Domain::MediaKind::Raw);
    CHECK(catalog.Spec(TypeId{9999}).key == catalog.Spec(TypeId{0}).key);
}

TEST_CASE("Types.TwoCatalogsDoNotShareIds") {
    TypeCatalog one;
    TypeCatalog two;
    TypeSpec extra;
    extra.key = "extra";
    extra.label = "Extra";
    TypeSpec shared;
    shared.key = "shared";
    shared.label = "Shared";
    one.Register("m", extra);
    const TypeId inOne = one.Register("m", shared);
    const TypeId inTwo = two.Register("m", shared);
    CHECK(inOne != inTwo);
    CHECK_FALSE(two.Find("m.extra").Valid());
    CHECK(two.Count() == one.Count() - 1);
    CHECK(two.Spec(inOne).label != "Shared");
}

TEST_CASE("Types.CatalogRegisterFromTwoThreads") {
    TypeCatalog catalog;
    auto work = [&catalog](int base) {
        for (int i = 0; i < 500; ++i) {
            TypeSpec spec;
            spec.key = "t" + std::to_string(base + i);
            catalog.Register("m", spec);
        }
    };
    std::thread a(work, 0);
    std::thread b(work, 500);
    a.join();
    b.join();
    CHECK(catalog.Count() == 1003);
    CHECK(catalog.All().size() == 1003);
}

TEST_CASE("Types.MediaKindNamesAreUnique") {
    std::set<std::string_view> names;
    for (int i = 0; i <= static_cast<int>(Domain::MediaKind::Raw); ++i) {
        const auto kind = static_cast<Domain::MediaKind>(i);
        const std::string_view name = Domain::Name(kind);
        CHECK_FALSE(name.empty());
        CHECK(names.insert(name).second);
        CHECK(Name(Domain::DefaultIcon(kind)).size() > 0);
    }
    CHECK(names.size() == 15);
}
