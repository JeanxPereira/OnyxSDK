#include <doctest/doctest.h>

#include <OnyxBox/Module.h>
#include <OnyxBox/Writer.h>

#include <Onyx/Core/Modules/EntryAccess.h>
#include <Onyx/Core/Modules/Workspace.h>

#include <memory>
#include <string>
#include <vector>

#include "Support/BoxFile.h"

using namespace Onyx;

namespace {

// Entries in order: txt, mesh, img.
struct Rig {
    Modules::Workspace ws;
    Tests::BoxFile file;

    Rig() : file("entryaccess.obx", Build()) { REQUIRE(ws.AddModule(std::make_unique<OnyxBox::Module>()).ok()); }

    static std::vector<std::byte> Build() {
        OnyxBox::Writer writer;
        writer.AddText("txt", "hello box");
        writer.AddMesh("mesh", {1, 0, 0, 1}, {0, 0, 0}, {1, 1, 1});
        writer.AddImage("img", 2, 2, std::vector<std::byte>(16, std::byte{0x40}));
        return writer.Build();
    }
};

}

TEST_CASE("FileOf is null for an out-of-range file index") {
    Rig rig;
    Modules::ScopedDocument opened(rig.ws, rig.file.path, {});
    REQUIRE(opened.ok());

    Domain::ByteRange range = opened.doc().roots[0].source;
    CHECK(Modules::FileOf(opened.doc(), range) != nullptr);
    range.fileIndex = static_cast<uint32_t>(opened.doc().files.size());
    CHECK(Modules::FileOf(opened.doc(), range) == nullptr);
}

TEST_CASE("ReaderOf reads the entry's bytes") {
    Rig rig;
    Modules::ScopedDocument opened(rig.ws, rig.file.path, {});
    REQUIRE(opened.ok());

    auto reader = Modules::ReaderOf(opened.doc(), opened.doc().roots[0]);
    REQUIRE(reader.has_value());
    const std::vector<std::byte> bytes = reader->ReadAll();
    CHECK(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()) == "hello box");

    Domain::AssetEntry stray = opened.doc().roots[0];
    stray.source.fileIndex = 99;
    CHECK_FALSE(Modules::ReaderOf(opened.doc(), stray).has_value());
}

TEST_CASE("ScopedDocument closes on scope exit") {
    Rig rig;
    Modules::DocumentId id = 0;
    {
        Modules::ScopedDocument opened(rig.ws, rig.file.path, {});
        REQUIRE(opened.ok());
        id = opened.id();
        CHECK(rig.ws.Get(id) == &opened.doc());
    }
    CHECK(rig.ws.Get(id) == nullptr);
}

TEST_CASE("ScopedDocument is not ok when the hint names no module") {
    Rig rig;
    Modules::ScopedDocument opened(rig.ws, rig.file.path, "no-such-module");
    CHECK_FALSE(opened.ok());
}

TEST_CASE("DecodeSceneOf returns a scene for a mesh and null for an image") {
    Rig rig;
    Modules::ScopedDocument opened(rig.ws, rig.file.path, {});
    REQUIRE(opened.ok());
    Services::Progress progress;

    CHECK(Modules::DecodeSceneOf(rig.ws, opened.doc(), opened.doc().roots[1], progress) != nullptr);
    CHECK(Modules::DecodeSceneOf(rig.ws, opened.doc(), opened.doc().roots[2], progress) == nullptr);
}
