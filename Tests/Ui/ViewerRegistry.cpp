#include <doctest/doctest.h>

#include <Onyx/Core/Modules/DecoderRegistry.h>
#include <Onyx/Core/Types/TypeCatalog.h>
#include <Onyx/Ui/ViewerRegistry.h>

using namespace Onyx;
using namespace Onyx::Ui;
using Domain::MediaKind;

namespace {

Types::TypeId AddType(Types::TypeCatalog& catalog, const char* key, MediaKind media) {
    Types::TypeSpec spec;
    spec.key = key;
    spec.label = key;
    spec.media = media;
    return catalog.Register("test", spec);
}

ViewerSpec Spec(const char* id, std::variant<MediaKind, Types::TypeKey> target, int priority = 0) {
    ViewerSpec s;
    s.id = id;
    s.target = std::move(target);
    s.priority = priority;
    s.create = [](OpenRequest&) { return std::unique_ptr<IDocumentView>(); };
    return s;
}

void GiveImage(Modules::DecoderRegistry& decoders, Types::TypeId id) {
    decoders.Image(id, [](Modules::DecodeContext&) { return std::optional<Domain::Texture>(); });
}

}

TEST_CASE("Route prefers a TypeKey target over a MediaKind target") {
    Types::TypeCatalog catalog;
    Modules::DecoderRegistry decoders;
    const auto png = AddType(catalog, "png", MediaKind::Image);
    GiveImage(decoders, png);

    ViewerRegistry reg;
    reg.Register(Spec("generic", MediaKind::Image, 100));
    reg.Register(Spec("special", Types::TypeKey{"test.png"}, 0));

    const ViewerSpec* routed = reg.Route(catalog, decoders, png);
    REQUIRE(routed != nullptr);
    CHECK(routed->id == "special");
}

TEST_CASE("Route breaks ties by priority, then registration order") {
    Types::TypeCatalog catalog;
    Modules::DecoderRegistry decoders;
    const auto png = AddType(catalog, "png", MediaKind::Image);
    GiveImage(decoders, png);

    ViewerRegistry reg;
    reg.Register(Spec("a", MediaKind::Image, 1));
    reg.Register(Spec("b", MediaKind::Image, 5));
    reg.Register(Spec("c", MediaKind::Image, 5));
    CHECK(reg.Route(catalog, decoders, png)->id == "b");

    reg.Unregister("b");
    CHECK(reg.Route(catalog, decoders, png)->id == "c");
}

TEST_CASE("Route returns null for an image type with no image decoder") {
    Types::TypeCatalog catalog;
    Modules::DecoderRegistry decoders;
    const auto png = AddType(catalog, "png", MediaKind::Image);

    ViewerRegistry reg;
    reg.Register(Spec("image", MediaKind::Image));
    CHECK(reg.Route(catalog, decoders, png) == nullptr);

    GiveImage(decoders, png);
    CHECK(reg.Route(catalog, decoders, png) != nullptr);
}

TEST_CASE("Raw and unknown media need no decoder") {
    Types::TypeCatalog catalog;
    Modules::DecoderRegistry decoders;
    const auto blob = AddType(catalog, "blob", MediaKind::Raw);
    const auto mystery = AddType(catalog, "mystery", MediaKind::Unknown);

    ViewerRegistry reg;
    reg.Register(Spec("hex", MediaKind::Raw));
    CHECK(reg.Route(catalog, decoders, blob)->id == "hex");
    CHECK(reg.Route(catalog, decoders, mystery)->id == "hex");
}

TEST_CASE("Route ignores viewers for other media and invalid types") {
    Types::TypeCatalog catalog;
    Modules::DecoderRegistry decoders;
    const auto txt = AddType(catalog, "txt", MediaKind::Text);
    decoders.Text(txt, [](Modules::DecodeContext&) { return std::optional<Domain::Text>(); });

    ViewerRegistry reg;
    reg.Register(Spec("image", MediaKind::Image));
    CHECK(reg.Route(catalog, decoders, txt) == nullptr);
    CHECK(reg.Route(catalog, decoders, Types::TypeId{static_cast<uint32_t>(catalog.Count())}) == nullptr);
    CHECK(reg.All().size() == 1);
}

TEST_CASE("The default type id routes to a Raw viewer") {
    Types::TypeCatalog catalog;
    Modules::DecoderRegistry decoders;

    ViewerRegistry reg;
    reg.Register(Spec("hex", MediaKind::Raw));
    const ViewerSpec* routed = reg.Route(catalog, decoders, Types::TypeId{});
    REQUIRE(routed != nullptr);
    CHECK(routed->id == "hex");
}

TEST_CASE("a TypeKey spec with anyCapability routes only when the type reports it") {
    Types::TypeCatalog catalog;
    Modules::DecoderRegistry decoders;
    const auto clip = AddType(catalog, "clip", MediaKind::Raw);
    const auto other = AddType(catalog, "other", MediaKind::Raw);
    decoders.Any(clip, "stream", [](Modules::DecodeContext&) { return std::shared_ptr<void>(); });

    ViewerSpec streaming = Spec("streaming", Types::TypeKey{"test.clip"}, 5);
    streaming.anyCapability = "stream";
    ViewerSpec streamingOther = Spec("streamingOther", Types::TypeKey{"test.other"}, 5);
    streamingOther.anyCapability = "stream";

    ViewerRegistry reg;
    reg.Register(Spec("hex", MediaKind::Raw, -10));
    reg.Register(std::move(streaming));
    reg.Register(std::move(streamingOther));

    CHECK(reg.Route(catalog, decoders, clip)->id == "streaming");
    CHECK(reg.Route(catalog, decoders, other)->id == "hex");
}

TEST_CASE("a MediaKind spec with anyCapability does not apply to a type lacking it") {
    Types::TypeCatalog catalog;
    Modules::DecoderRegistry decoders;
    const auto clip = AddType(catalog, "clip", MediaKind::Raw);
    const auto other = AddType(catalog, "other", MediaKind::Raw);
    decoders.Any(clip, "stream", [](Modules::DecodeContext&) { return std::shared_ptr<void>(); });

    ViewerSpec streaming = Spec("streaming", MediaKind::Raw, 100);
    streaming.anyCapability = "stream";

    ViewerRegistry reg;
    reg.Register(Spec("hex", MediaKind::Raw, -10));
    reg.Register(std::move(streaming));

    CHECK(reg.Route(catalog, decoders, clip)->id == "streaming");
    CHECK(reg.Route(catalog, decoders, other)->id == "hex");
}
