#include <doctest/doctest.h>

#include <Onyx/App/ViewerOpening.h>
#include <Onyx/App/ViewerRouting.h>
#include <Onyx/Modules/DecoderRegistry.h>
#include <Onyx/Modules/Workspace.h>
#include <Onyx/Parsers/SceneNode.h>
#include <Onyx/Parsers/TextureData.h>
#include <Onyx/Types/TypeId.h>

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>

using namespace Onyx::App;
using namespace Onyx::Modules;
using namespace Onyx::Types;
using namespace Onyx::Parsers;

namespace {

struct FakeMesh : SceneData {};
struct FakeTexture : TextureData {};

void RegisterCapabilities(DecoderRegistry& reg, TypeId type, bool scene, bool image, bool text) {
    if (scene) {
        reg.Scene(type, [](DecodeContext&) { return std::make_unique<FakeMesh>(); });
    }
    if (image) {
        reg.Image(type, [](DecodeContext&) { return std::make_unique<FakeTexture>(); });
    }
    if (text) {
        reg.Text(type, [](DecodeContext&) { return std::make_optional(TextOut{"hello box", ""}); });
    }
}

std::filesystem::path write_temp_file(const std::string& name, const std::string& contents) {
    auto path = std::filesystem::temp_directory_path() / name;
    std::ofstream f(path, std::ios::binary);
    f << contents;
    f.close();
    return path;
}

// Fixture for OpenSelection (the App/ViewerOpening.h seam): a document
// with one Text entry, one Image entry, one entry of a type no decoder
// is registered for, and one Failed entry -- enough to exercise every
// branch OpenSelection has (decode+open, no-capability, Failed-skips).
struct RoutingFake : Onyx::Modules::IGameModule {
    Onyx::Types::TypeId textType;
    Onyx::Types::TypeId imageType;
    Onyx::Types::TypeId blobType;    // deliberately no decoder registered
    Onyx::Types::TypeId corruptType; // has a Text decoder, but it salvage-fails

    ModuleInfo Info() const override { return ModuleInfo{"routingfake", "RoutingFake", {}, {}}; }

    ProbeResult Probe(const ProbeInput& in) const override {
        if (!in.header.empty() && in.header[0] == std::byte{'R'}) return ProbeResult{90, "'R' magic"};
        return ProbeResult{0, "no magic"};
    }

    void RegisterTypes(Onyx::Types::TypeRegistrar& r) override {
        Onyx::Types::TypeInfo text;
        text.key = "text";
        text.label = "Text";
        textType = r.Add(text);

        Onyx::Types::TypeInfo image;
        image.key = "image";
        image.label = "Image";
        imageType = r.Add(image);

        Onyx::Types::TypeInfo blob;
        blob.key = "blob";
        blob.label = "Blob";
        blobType = r.Add(blob);

        Onyx::Types::TypeInfo corrupt;
        corrupt.key = "corrupt";
        corrupt.label = "Corrupt";
        corruptType = r.Add(corrupt);
    }

    void RegisterDecoders(DecoderRegistry& reg) override {
        reg.Text(textType, [](DecodeContext&) { return std::make_optional(TextOut{"hello box", ""}); });
        reg.Image(imageType, [](DecodeContext&) { return std::make_unique<FakeTexture>(); });
        // blobType intentionally left with no decoder.
        // corruptType HAS a Text decoder, but it always salvage-fails
        // (returns nullopt) -- the "capability exists, decode still
        // fails" case, distinct from blobType's "no capability at all".
        reg.Text(corruptType, [](DecodeContext&) { return std::nullopt; });
    }

    ParseResult ParseContainer(ContainerContext& ctx) override {
        Onyx::Domain::AssetEntry textEntry;
        textEntry.name = "hello.txt";
        textEntry.typeId = textType;
        ctx.roots.push_back(textEntry); // roots[0]

        Onyx::Domain::AssetEntry imageEntry;
        imageEntry.name = "gradient.img";
        imageEntry.typeId = imageType;
        ctx.roots.push_back(imageEntry); // roots[1]

        Onyx::Domain::AssetEntry blobEntry;
        blobEntry.name = "raw.blob";
        blobEntry.typeId = blobType;
        ctx.roots.push_back(blobEntry); // roots[2]

        Onyx::Domain::AssetEntry failedEntry;
        failedEntry.name = "broken.txt";
        failedEntry.typeId = textType;
        failedEntry.flags = Onyx::Domain::NodeFlags::Failed;
        ctx.diags.Report(Onyx::Services::Diag{
            Onyx::Services::Severity::Error, "routingfake.entry.bad", "entry 3 corrupt", std::nullopt});
        ctx.roots.push_back(failedEntry); // roots[3]

        Onyx::Domain::AssetEntry corruptEntry;
        corruptEntry.name = "corrupt.txt";
        corruptEntry.typeId = corruptType;
        ctx.roots.push_back(corruptEntry); // roots[4] -- capability exists, Decode* salvage-fails

        return ParseResult{true};
    }
};

} // namespace

TEST_CASE("RouteForType: all 8 capability permutations, priority Scene > Image > Text") {
    // mask bit 2 = Scene, bit 1 = Image, bit 0 = Text -- every combination
    // of the three capabilities being registered or not, including none
    // (mask 0, which must route to None) and all three at once (mask 7,
    // which must still prefer Scene).
    for (uint32_t mask = 0; mask < 8; ++mask) {
        const bool scene = (mask & 0b100) != 0;
        const bool image = (mask & 0b010) != 0;
        const bool text  = (mask & 0b001) != 0;

        DecoderRegistry reg;
        // TypeId 0 is reserved for Unknown/invalid -- offset so every
        // permutation gets a distinct, valid type.
        TypeId type{mask + 1};
        RegisterCapabilities(reg, type, scene, image, text);

        ViewerKind expected = ViewerKind::None;
        if (scene)      expected = ViewerKind::Scene;
        else if (image) expected = ViewerKind::Image;
        else if (text)  expected = ViewerKind::Text;

        CAPTURE(mask);
        CAPTURE(scene);
        CAPTURE(image);
        CAPTURE(text);
        CHECK(RouteForType(reg, type) == expected);
    }
}

TEST_CASE("RouteForType: unregistered TypeId routes to None") {
    DecoderRegistry reg;
    reg.Scene(TypeId{1}, [](DecodeContext&) { return std::make_unique<FakeMesh>(); });

    TypeId unregistered{999};
    CHECK(RouteForType(reg, unregistered) == ViewerKind::None);
}

TEST_CASE("RouteForType: invalid (default) TypeId routes to None") {
    DecoderRegistry reg;
    // Register every capability under some other type -- the invalid
    // TypeId{} (value 0) must never accidentally alias one of them.
    RegisterCapabilities(reg, TypeId{1}, true, true, true);

    TypeId invalid{};
    CHECK_FALSE(invalid.valid());
    CHECK(RouteForType(reg, invalid) == ViewerKind::None);
}

// ── ViewerOpening: OpenSelection (Include/Onyx/App/ViewerOpening.h) ───────
// The seam behind the double-click glue: decodes on the caller's thread
// and hands the result to an injected ViewerOpener, without ever touching
// ImGui or a real viewer class -- these tests supply recording callbacks
// instead. Titled "ViewerOpening: ..." rather than "OpenSelection: ..." so
// the OnyxSelection ctest filter (`*Selection:*`, Tests/CMakeLists.txt),
// which targets Tests/selection_test.cpp's "Selection: ..." cases, does
// not also pick these up by substring match -- OnyxViewerRouting is what
// runs these instead (see its own filter below).

TEST_CASE("ViewerOpening: Text entry decodes and invokes openText") {
    auto tmp = write_temp_file("onyx_openselection_text.bin", "R____");
    Onyx::Modules::DocumentId id = 0;
    {
        Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
        ws.AddModule(std::make_unique<RoutingFake>());

        id = ws.Open(tmp);
        REQUIRE(id != 0);
        REQUIRE(ws.Get(id));
        REQUIRE(ws.Get(id)->roots.size() == 5);

        bool opened = false;
        std::string openedName, openedText;
        ViewerOpener opener;
        opener.openText = [&](std::string name, TextOut text) {
            opened = true;
            openedName = name;
            openedText = text.text;
        };

        ViewerKind kind = OpenSelection(ws, SelectionChanged{id, NodePath{{0}}}, opener);

        CHECK(kind == ViewerKind::Text);
        CHECK(opened);
        CHECK(openedName == "hello.txt");
        CHECK(openedText == "hello box");
    } // ~Workspace closes the Document's OsFile, releasing the OS handle
      // (Windows refuses to remove an open file) -- see workspace_test.cpp.

    std::filesystem::remove(tmp);
}

TEST_CASE("ViewerOpening: Image entry decodes and invokes openImage") {
    auto tmp = write_temp_file("onyx_openselection_image.bin", "R____");
    {
        Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
        ws.AddModule(std::make_unique<RoutingFake>());

        auto id = ws.Open(tmp);
        REQUIRE(id != 0);

        bool opened = false;
        std::string openedName;
        ViewerOpener opener;
        opener.openImage = [&](std::string name, std::unique_ptr<TextureData> tex) {
            opened = true;
            openedName = name;
            CHECK(tex != nullptr);
        };

        ViewerKind kind = OpenSelection(ws, SelectionChanged{id, NodePath{{1}}}, opener);

        CHECK(kind == ViewerKind::Image);
        CHECK(opened);
        CHECK(openedName == "gradient.img");
    }

    std::filesystem::remove(tmp);
}

TEST_CASE("ViewerOpening: entry with no decoder capability routes to None, opener untouched") {
    auto tmp = write_temp_file("onyx_openselection_none.bin", "R____");
    {
        Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
        ws.AddModule(std::make_unique<RoutingFake>());

        auto id = ws.Open(tmp);
        REQUIRE(id != 0);

        bool anyOpened = false;
        ViewerOpener opener;
        opener.openText  = [&](std::string, TextOut) { anyOpened = true; };
        opener.openImage = [&](std::string, std::unique_ptr<TextureData>) { anyOpened = true; };
        opener.openScene = [&](std::string, std::unique_ptr<SceneData>) { anyOpened = true; };

        ViewerKind kind = OpenSelection(ws, SelectionChanged{id, NodePath{{2}}}, opener);

        CHECK(kind == ViewerKind::None);
        CHECK_FALSE(anyOpened);

        // roots[4] is the opposite miss: corruptType DOES have a Text
        // decoder registered (RoutingFake::RegisterDecoders), but that
        // decoder always salvage-fails (returns nullopt) -- distinct
        // from roots[2]'s "no capability at all" above. OpenSelection
        // must still route to None and never call the opener (it used
        // to do so silently; it now also logs via LOG_WARN, which this
        // seam-level test has no cheap way to capture/assert).
        anyOpened = false;
        kind = OpenSelection(ws, SelectionChanged{id, NodePath{{4}}}, opener);

        CHECK(kind == ViewerKind::None);
        CHECK_FALSE(anyOpened);
    }

    std::filesystem::remove(tmp);
}

TEST_CASE("ViewerOpening: Failed entry is never decoded, opener untouched") {
    auto tmp = write_temp_file("onyx_openselection_failed.bin", "R____");
    {
        Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
        ws.AddModule(std::make_unique<RoutingFake>());

        auto id = ws.Open(tmp);
        REQUIRE(id != 0);

        bool anyOpened = false;
        ViewerOpener opener;
        opener.openText = [&](std::string, TextOut) { anyOpened = true; };

        // roots[3] is the Failed entry -- same type (textType) as
        // roots[0], which DOES have a Text decoder, proving the Failed
        // check runs before routing/decoding, not just when no
        // capability exists.
        ViewerKind kind = OpenSelection(ws, SelectionChanged{id, NodePath{{3}}}, opener);

        CHECK(kind == ViewerKind::None);
        CHECK_FALSE(anyOpened);
    }

    std::filesystem::remove(tmp);
}

TEST_CASE("ViewerOpening: a stale/unresolvable path routes to None") {
    auto tmp = write_temp_file("onyx_openselection_stale.bin", "R____");
    {
        Onyx::Modules::Workspace ws(Onyx::Types::TypeCatalog::Get());
        ws.AddModule(std::make_unique<RoutingFake>());

        auto id = ws.Open(tmp);
        REQUIRE(id != 0);

        ViewerOpener opener; // no callbacks set -- must never be needed
        ViewerKind kind = OpenSelection(ws, SelectionChanged{id, NodePath{{99}}}, opener);
        CHECK(kind == ViewerKind::None);

        // An unknown DocumentId (e.g. the document was Closed) also
        // routes to None rather than crashing.
        kind = OpenSelection(ws, SelectionChanged{id + 12345, NodePath{{0}}}, opener);
        CHECK(kind == ViewerKind::None);
    }

    std::filesystem::remove(tmp);
}
