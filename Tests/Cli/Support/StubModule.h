#pragma once

#include <Onyx/Core/Modules/DecoderRegistry.h>
#include <Onyx/Core/Modules/Document.h>
#include <Onyx/Core/Modules/GameModule.h>
#include <Onyx/Core/Schema/Schema.h>
#include <Onyx/Core/Vfs/MemoryFile.h>

#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace Onyx::Tests {

struct StubEntry {
    std::string name;
    std::string type = "blob";
    uint32_t fileIndex = 0;
    uint64_t offset = 0;
    uint64_t size = 0;
    bool failed = false;
    std::vector<StubEntry> children;
};

struct StubOptions {
    std::string id = "stub";
    int probeScore = 90;
    std::vector<std::string> hints;
    std::vector<StubEntry> script;
    std::vector<std::string> extraFiles;
    bool reportParseError = false;
};

// Parses a scripted entry tree and registers one decoder per capability. Type keys: blob (no
// decoder), text, scene, image, audio, table, schema, any, badimage (reports and fails).
class StubModule : public Modules::IGameModule {
public:
    explicit StubModule(StubOptions options = {}) : m_options(std::move(options)) {}

    Modules::ModuleInfo Info() const override { return {m_options.id, m_options.id, m_options.hints, {}}; }

    Modules::ProbeResult Probe(const Modules::ProbeInput&) const override { return {m_options.probeScore, "stub"}; }

    void RegisterTypes(Types::TypeRegistrar& registrar) override {
        Add(registrar, "blob", Domain::MediaKind::Raw);
        Add(registrar, "text", Domain::MediaKind::Text);
        Add(registrar, "scene", Domain::MediaKind::Mesh);
        Add(registrar, "image", Domain::MediaKind::Image);
        Add(registrar, "audio", Domain::MediaKind::Audio);
        Add(registrar, "table", Domain::MediaKind::Table);
        Add(registrar, "schema", Domain::MediaKind::Raw);
        Add(registrar, "any", Domain::MediaKind::Raw);
        Add(registrar, "badimage", Domain::MediaKind::Image);
    }

    void RegisterDecoders(Modules::DecoderRegistry& decoders) override {
        decoders.Text(m_types["text"], [](Modules::DecodeContext& ctx) {
            const std::vector<std::byte> bytes = ctx.reader.ReadAll();
            Domain::Text text;
            text.text.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            return std::make_optional(std::move(text));
        });
        decoders.Scene(m_types["scene"], [](Modules::DecodeContext&) {
            auto scene = std::make_unique<Domain::Scene>();
            Domain::MeshPart part;
            part.positions.assign(4, glm::vec3(0.0f));
            part.indices = {0, 1, 2, 0, 2, 3};
            scene->parts.push_back(std::move(part));
            scene->materials.emplace_back();
            return scene;
        });
        decoders.Image(m_types["image"], [](Modules::DecodeContext&) {
            Domain::Texture texture;
            texture.width = 2;
            texture.height = 3;
            texture.pixels.assign(24, std::byte{0});
            return std::make_optional(std::move(texture));
        });
        decoders.Image(m_types["badimage"], [](Modules::DecodeContext& ctx) -> std::optional<Domain::Texture> {
            ctx.diags.Report({Services::Severity::Warning, "stub.image.size-mismatch", "declared size lies", std::nullopt});
            return std::nullopt;
        });
        decoders.Audio(m_types["audio"], [](Modules::DecodeContext&) {
            Domain::Audio audio;
            audio.sampleRate = 8000;
            audio.channels = 2;
            audio.samples.assign(16000, 0.0f);
            return std::make_optional(std::move(audio));
        });
        decoders.Table(m_types["table"], [](Modules::DecodeContext&) {
            Domain::Table table;
            table.columns = {"a", "b"};
            table.rows = {{"1", "2"}, {"3", "4"}, {"5", "6"}};
            return std::make_optional(std::move(table));
        });
        auto schema = std::make_shared<Schema::Schema>("Header");
        schema->Field("version", Schema::FieldType::U8);
        decoders.Schema(m_types["schema"], schema);
        decoders.Any(m_types["any"], "thing", [](Modules::DecodeContext&) { return std::make_shared<int>(1); });
    }

    Modules::ParseResult ParseContainer(Modules::ContainerContext& ctx) override {
        for (const std::string& bytes : m_options.extraFiles) {
            std::vector<std::byte> copy(bytes.size());
            std::memcpy(copy.data(), bytes.data(), bytes.size());
            ctx.files.push_back(std::make_shared<Vfs::MemoryFile>(std::move(copy)));
        }
        if (m_options.reportParseError) {
            ctx.diags.Report({Services::Severity::Error, "stub.parse.bad", "a record is corrupt", std::nullopt});
        }
        for (const StubEntry& e : m_options.script) ctx.roots.push_back(Build(e));
        return {true};
    }

private:
    void Add(Types::TypeRegistrar& registrar, const char* key, Domain::MediaKind media) {
        Types::TypeSpec spec;
        spec.key = key;
        spec.label = key;
        spec.media = media;
        m_types[key] = registrar.Add(spec);
    }

    Domain::AssetEntry Build(const StubEntry& e) {
        Domain::AssetEntry entry;
        entry.name = e.name;
        entry.type = m_types[e.type];
        entry.source = {e.fileIndex, e.offset, e.size};
        if (e.failed) entry.flags = Domain::NodeFlags::Failed;
        for (const StubEntry& child : e.children) entry.children.push_back(Build(child));
        return entry;
    }

    StubOptions m_options;
    std::map<std::string, Types::TypeId> m_types;
};

}
