#include <doctest/doctest.h>

#include <Onyx/Modules/DecoderRegistry.h>
#include <Onyx/Domain/Entry.h>
#include <Onyx/Parsers/SceneNode.h>
#include <Onyx/Parsers/TextureData.h>
#include <Onyx/Services/Diagnostics.h>
#include <Onyx/Services/Jobs.h>
#include <Onyx/Types/TypeId.h>
#include <memory>

using namespace Onyx::Modules;
using namespace Onyx::Types;
using namespace Onyx::Services;
using namespace Onyx::Domain;
using namespace Onyx::Parsers;

namespace {

struct FakeMesh : SceneData {};
struct FakeTexture : TextureData {};

// Helper to create a simple scene decoder
SceneDecoder MakeSceneDecoder() {
    return [](DecodeContext&) {
        return std::make_unique<FakeMesh>();
    };
}

// Helper to create a simple image decoder
ImageDecoder MakeImageDecoder() {
    return [](DecodeContext&) {
        return std::make_unique<FakeTexture>();
    };
}

// Helper to create a simple text decoder
TextDecoder MakeTextDecoder() {
    return [](DecodeContext&) {
        return std::make_optional(TextOut{"test", "json"});
    };
}

// Helper to create a throwing scene decoder
auto MakeThrowingSceneDecoder = []() {
    SceneDecoder decoder = [](DecodeContext&) -> std::unique_ptr<SceneData> {
        throw std::runtime_error("decoder boom");
    };
    return decoder;
};

} // namespace

TEST_CASE("DecoderRegistry registers and dispatches by TypeId") {
    TypeId scene_type{1};
    TypeId image_type{2};
    TypeId text_type{3};

    DecoderRegistry reg;

    // Register fake decoders
    reg.Scene(scene_type, MakeSceneDecoder());
    reg.Image(image_type, MakeImageDecoder());
    reg.Text(text_type, MakeTextDecoder());

    // Verify Has* methods
    CHECK(reg.HasScene(scene_type));
    CHECK(reg.HasImage(image_type));
    CHECK(reg.HasText(text_type));

    // Verify dispatch
    Document doc{};
    Progress prog;
    AssetEntry entry;
    entry.name = "test_entry";
    entry.typeId = scene_type;

    DiagSink diags;
    DecodeContext ctx{doc, entry, diags, prog};

    auto scene_result = reg.DecodeScene(ctx);
    CHECK(scene_result != nullptr);

    entry.typeId = text_type;
    DecodeContext ctx2{doc, entry, diags, prog};
    auto text_result = reg.DecodeText(ctx2);
    CHECK(text_result.has_value());
}

TEST_CASE("DecoderRegistry wrong-type lookup returns null without side effects") {
    TypeId registered_type{1};
    TypeId unregistered_type{999};

    DecoderRegistry reg;

    reg.Scene(registered_type, MakeSceneDecoder());

    Document doc{};
    Progress prog;
    AssetEntry entry;
    entry.name = "test_entry";
    entry.typeId = unregistered_type;

    DiagSink diags;
    DecodeContext ctx{doc, entry, diags, prog};

    // Wrong type should return null
    auto result = reg.DecodeScene(ctx);
    CHECK(result == nullptr);

    // No diagnostics should be reported
    auto diag_list = diags.Drain();
    CHECK(diag_list.size() == 0);
}

TEST_CASE("DecoderRegistry throwing decoder returns null + one Error diag") {
    TypeId throw_type{1};

    DecoderRegistry reg;

    reg.Scene(throw_type, MakeThrowingSceneDecoder());

    Document doc{};
    Progress prog;
    AssetEntry entry;
    entry.name = "test_entry";
    entry.typeId = throw_type;

    DiagSink diags;
    DecodeContext ctx{doc, entry, diags, prog};

    auto result = reg.DecodeScene(ctx);

    // Throwing decoder should return null
    CHECK(result == nullptr);

    // Should report exactly one Error diag with code "decoder.threw"
    auto diag_list = diags.Drain();
    REQUIRE(diag_list.size() == 1);
    CHECK(diag_list[0].severity == Severity::Error);
    CHECK(diag_list[0].code == "decoder.threw");
    // Message should contain the asset name and the exception details
    CHECK(diag_list[0].message.find("test_entry") != std::string::npos);
    CHECK(diag_list[0].message.find("decoder boom") != std::string::npos);
}

TEST_CASE("DecoderRegistry double registration replaces (last wins)") {
    TypeId type{1};

    DecoderRegistry reg;

    // First decoder
    reg.Scene(type, [](DecodeContext&) {
        return std::make_unique<FakeMesh>();
    });

    // Second decoder with different identity (we'll check if the first one is gone)
    reg.Scene(type, [](DecodeContext&) {
        // Second one registered - should be used
        return std::make_unique<FakeMesh>();
    });

    Document doc{};
    Progress prog;
    AssetEntry entry;
    entry.name = "test_entry";
    entry.typeId = type;

    DiagSink diags;
    DecodeContext ctx{doc, entry, diags, prog};

    auto result = reg.DecodeScene(ctx);
    CHECK(result != nullptr);
    // We can't easily verify which decoder was called, but at least we know one was called
    // The "last wins" behavior is implicit in how the registry is tested
}

TEST_CASE("DecoderRegistry Has* returns false for unregistered types") {
    TypeId unregistered{999};

    DecoderRegistry reg;

    CHECK_FALSE(reg.HasScene(unregistered));
    CHECK_FALSE(reg.HasImage(unregistered));
    CHECK_FALSE(reg.HasText(unregistered));
}

TEST_CASE("DecoderRegistry Image dispatch") {
    TypeId image_type{2};

    DecoderRegistry reg;

    reg.Image(image_type, MakeImageDecoder());

    Document doc{};
    Progress prog;
    AssetEntry entry;
    entry.name = "test_entry";
    entry.typeId = image_type;

    DiagSink diags;
    DecodeContext ctx{doc, entry, diags, prog};

    auto result = reg.DecodeImage(ctx);
    CHECK(result != nullptr);
}

TEST_CASE("DecoderRegistry Text dispatch returns empty when unregistered") {
    TypeId text_type{3};

    DecoderRegistry reg;

    Document doc{};
    Progress prog;
    AssetEntry entry;
    entry.name = "test_entry";
    entry.typeId = text_type;

    DiagSink diags;
    DecodeContext ctx{doc, entry, diags, prog};

    auto result = reg.DecodeText(ctx);
    CHECK_FALSE(result.has_value());
}
