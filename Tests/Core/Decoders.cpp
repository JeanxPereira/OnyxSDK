#include <doctest/doctest.h>

#include <Onyx/Core/Modules/Capabilities.h>
#include <Onyx/Core/Modules/DecodeContext.h>
#include <Onyx/Core/Modules/DecoderRegistry.h>
#include <Onyx/Core/Schema/Schema.h>
#include <Onyx/Core/Services/Diagnostics.h>
#include <Onyx/Core/Services/Jobs.h>
#include <Onyx/Core/Services/Settings.h>
#include <Onyx/Core/Vfs/MemoryFile.h>

#include <array>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using namespace Onyx;
using namespace Onyx::Modules;
using Onyx::Types::TypeId;

namespace {

struct Fixture {
    Domain::AssetEntry entry;
    Services::DiagSink diags;
    Services::Progress progress;
    Services::Settings settings = Services::Settings::Empty();
    std::shared_ptr<void> state;

    explicit Fixture(TypeId type, std::string name = "test_entry") {
        entry.name = std::move(name);
        entry.type = type;
    }

    DecodeContext Ctx() {
        return DecodeContext{nullptr,
                             entry,
                             Vfs::Reader(std::make_shared<Vfs::MemoryFile>(std::vector<std::byte>{})),
                             diags,
                             progress,
                             settings,
                             state};
    }
};

SceneDecoder MakeSceneDecoder() {
    return [](DecodeContext&) { return std::make_unique<Domain::Scene>(); };
}

ImageDecoder MakeImageDecoder() {
    return [](DecodeContext&) {
        Domain::Texture t;
        t.width = 1;
        return std::make_optional(t);
    };
}

TextDecoder MakeTextDecoder() {
    return [](DecodeContext&) { return std::make_optional(Domain::Text{"test", "json"}); };
}

AudioDecoder MakeAudioDecoder() {
    return [](DecodeContext&) {
        Domain::Audio a;
        a.sampleRate = 44100;
        return std::make_optional(a);
    };
}

TableDecoder MakeTableDecoder() {
    return [](DecodeContext&) {
        Domain::Table t;
        t.columns = {"a"};
        return std::make_optional(t);
    };
}

AnyDecoder MakeAnyDecoder() {
    return [](DecodeContext&) { return std::shared_ptr<void>{}; };
}

}

TEST_CASE("Decoders.RegistersAndDispatchesByTypeId") {
    TypeId sceneType{1}, imageType{2}, textType{3};
    DecoderRegistry reg;
    reg.Scene(sceneType, MakeSceneDecoder());
    reg.Image(imageType, MakeImageDecoder());
    reg.Text(textType, MakeTextDecoder());

    CHECK(reg.Of(sceneType).scene);
    CHECK(reg.Of(imageType).image);
    CHECK(reg.Of(textType).text);

    Fixture f(sceneType);
    auto ctx = f.Ctx();
    CHECK(reg.DecodeScene(ctx) != nullptr);

    f.entry.type = textType;
    auto text = reg.DecodeText(ctx);
    REQUIRE(text.has_value());
    CHECK(text->text == "test");
    CHECK(text->language == "json");

    f.entry.type = imageType;
    auto image = reg.DecodeImage(ctx);
    REQUIRE(image.has_value());
    CHECK(image->width == 1);
}

TEST_CASE("Decoders.UnregisteredTypeReturnsNullWithoutDiags") {
    DecoderRegistry reg;
    reg.Scene(TypeId{1}, MakeSceneDecoder());

    Fixture f(TypeId{999});
    auto ctx = f.Ctx();
    CHECK(reg.DecodeScene(ctx) == nullptr);
    CHECK_FALSE(reg.DecodeImage(ctx).has_value());
    CHECK_FALSE(reg.DecodeText(ctx).has_value());
    CHECK_FALSE(reg.DecodeAudio(ctx).has_value());
    CHECK_FALSE(reg.DecodeTable(ctx).has_value());
    CHECK(reg.DecodeAny(ctx, "palette") == nullptr);
    CHECK(f.diags.Drain().empty());
}

TEST_CASE("Decoders.ThrowingDecoderReturnsNullWithOneErrorDiag") {
    TypeId type{1};
    DecoderRegistry reg;
    reg.Scene(type, [](DecodeContext&) -> std::unique_ptr<Domain::Scene> {
        throw std::runtime_error("decoder boom");
    });

    Fixture f(type);
    auto ctx = f.Ctx();
    CHECK(reg.DecodeScene(ctx) == nullptr);

    auto diags = f.diags.Drain();
    REQUIRE(diags.size() == 1);
    CHECK(diags[0].severity == Services::Severity::Error);
    CHECK(diags[0].code == "onyx.decode.threw");
    CHECK(diags[0].message == "decoder boom");
}

TEST_CASE("Decoders.NonStandardThrowReportsUnknownException") {
    TypeId type{1};
    DecoderRegistry reg;
    reg.Text(type, [](DecodeContext&) -> std::optional<Domain::Text> { throw 42; });

    Fixture f(type);
    auto ctx = f.Ctx();
    CHECK_FALSE(reg.DecodeText(ctx).has_value());

    auto diags = f.diags.Drain();
    REQUIRE(diags.size() == 1);
    CHECK(diags[0].code == "onyx.decode.threw");
    CHECK(diags[0].message == "unknown exception");
}

TEST_CASE("Decoders.DoubleRegistrationReplaces") {
    TypeId type{1};
    DecoderRegistry reg;
    reg.Text(type, [](DecodeContext&) { return std::make_optional(Domain::Text{"first", ""}); });
    reg.Text(type, [](DecodeContext&) { return std::make_optional(Domain::Text{"second", ""}); });

    Fixture f(type);
    auto ctx = f.Ctx();
    auto result = reg.DecodeText(ctx);
    REQUIRE(result.has_value());
    CHECK(result->text == "second");
}

TEST_CASE("Decoders.AudioAndTableDispatch") {
    TypeId audioType{4}, tableType{5};
    DecoderRegistry reg;
    reg.Audio(audioType, MakeAudioDecoder());
    reg.Table(tableType, MakeTableDecoder());

    Fixture f(audioType);
    auto ctx = f.Ctx();
    auto audio = reg.DecodeAudio(ctx);
    REQUIRE(audio.has_value());
    CHECK(audio->sampleRate == 44100);

    f.entry.type = tableType;
    auto table = reg.DecodeTable(ctx);
    REQUIRE(table.has_value());
    CHECK(table->columns.size() == 1);
}

TEST_CASE("Decoders.AnySlotRoundTrips") {
    TypeId type{7};
    DecoderRegistry reg;
    reg.Any(type, "palette", [](DecodeContext&) -> std::shared_ptr<void> {
        return std::make_shared<std::vector<int>>(std::vector<int>{1, 2, 3});
    });

    Fixture f(type);
    auto ctx = f.Ctx();
    auto result = reg.DecodeAny(ctx, "palette");
    REQUIRE(result != nullptr);
    auto palette = std::static_pointer_cast<std::vector<int>>(result);
    CHECK(*palette == std::vector<int>{1, 2, 3});

    CHECK(reg.DecodeAny(ctx, "other") == nullptr);
    CHECK(f.diags.Drain().empty());
}

TEST_CASE("Decoders.ThrowingAnyDecoderReturnsNullWithOneErrorDiag") {
    TypeId type{7};
    DecoderRegistry reg;
    reg.Any(type, "palette", [](DecodeContext&) -> std::shared_ptr<void> {
        throw std::runtime_error("any boom");
    });

    Fixture f(type);
    auto ctx = f.Ctx();
    CHECK(reg.DecodeAny(ctx, "palette") == nullptr);

    auto diags = f.diags.Drain();
    REQUIRE(diags.size() == 1);
    CHECK(diags[0].severity == Services::Severity::Error);
    CHECK(diags[0].code == "onyx.decode.threw");
    CHECK(diags[0].message == "any boom");
}

TEST_CASE("Decoders.CapabilitiesListsEverySlot") {
    TypeId type{9};
    DecoderRegistry reg;
    CHECK(reg.Of(type).Empty());

    reg.Scene(type, MakeSceneDecoder());
    reg.Image(type, MakeImageDecoder());
    reg.Text(type, MakeTextDecoder());
    reg.Audio(type, MakeAudioDecoder());
    reg.Table(type, MakeTableDecoder());
    reg.Any(type, "zeta", MakeAnyDecoder());
    reg.Any(type, "alpha", MakeAnyDecoder());
    reg.Schema(type, std::make_shared<const Onyx::Schema::Schema>("S"));

    auto caps = reg.Of(type);
    CHECK_FALSE(caps.Empty());
    CHECK(caps.scene);
    CHECK(caps.image);
    CHECK(caps.text);
    CHECK(caps.audio);
    CHECK(caps.table);
    CHECK(caps.schema);
    CHECK(caps.any == std::vector<std::string>{"alpha", "zeta"});

    CHECK(reg.Of(TypeId{10}).Empty());
}

TEST_CASE("Decoders.RemoveAllClearsEverySlotForTheIds") {
    TypeId a{1}, b{2}, keep{3};
    DecoderRegistry reg;
    for (TypeId id : {a, b, keep}) {
        reg.Scene(id, MakeSceneDecoder());
        reg.Image(id, MakeImageDecoder());
        reg.Text(id, MakeTextDecoder());
        reg.Audio(id, MakeAudioDecoder());
        reg.Table(id, MakeTableDecoder());
        reg.Any(id, "x", MakeAnyDecoder());
        reg.Schema(id, std::make_shared<const Onyx::Schema::Schema>("S"));
    }

    std::array<TypeId, 2> gone{a, b};
    reg.RemoveAll(gone);

    CHECK(reg.Of(a).Empty());
    CHECK(reg.Of(b).Empty());
    CHECK(reg.SchemaOf(a) == nullptr);
    auto kept = reg.Of(keep);
    CHECK(kept.scene);
    CHECK(kept.schema);
    CHECK(kept.any.size() == 1);
}

TEST_CASE("Decoders.SchemaOfReturnsRegisteredSchema") {
    TypeId type{1};
    DecoderRegistry reg;
    CHECK(reg.SchemaOf(type) == nullptr);

    auto schema = std::make_shared<const Onyx::Schema::Schema>("Header");
    reg.Schema(type, schema);
    CHECK(reg.SchemaOf(type) == schema);
    CHECK(reg.SchemaOf(TypeId{2}) == nullptr);
}
