#pragma once

#include <Onyx/Core/Modules/DecoderRegistry.h>
#include <Onyx/Core/Modules/Document.h>
#include <Onyx/Core/Modules/GameModule.h>

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace Onyx::Tests {

struct StubOptions {
    std::string id = "stub";
    int probeScore = 90;
    int entries = 0;
    bool throwOnParse = false;
    std::vector<std::string> hints;
};

class StubModule : public Modules::IGameModule {
public:
    explicit StubModule(StubOptions options = {}) : m_options(std::move(options)) {}

    Types::TypeId BlobType() const { return m_blob; }

    Modules::ModuleInfo Info() const override { return {m_options.id, m_options.id, m_options.hints, {}}; }

    Modules::ProbeResult Probe(const Modules::ProbeInput&) const override { return {m_options.probeScore, "stub"}; }

    void RegisterTypes(Types::TypeRegistrar& registrar) override {
        Types::TypeSpec spec;
        spec.key = "blob";
        spec.label = "Blob";
        spec.media = Domain::MediaKind::Raw;
        m_blob = registrar.Add(spec);
    }

    void RegisterDecoders(Modules::DecoderRegistry& decoders) override {
        decoders.Text(m_blob, [](Modules::DecodeContext&) { return std::nullopt; });
    }

    void RegisterSettings(Services::SettingsSchema& schema) override { schema.Bool(m_options.id + ".flag", true); }

    Modules::ParseResult ParseContainer(Modules::ContainerContext& ctx) override {
        if (m_options.throwOnParse) throw std::runtime_error("stub parse failure");
        for (int i = 0; i < m_options.entries; ++i) {
            Domain::AssetEntry entry;
            entry.name = "e" + std::to_string(i);
            entry.type = m_blob;
            entry.kind = Domain::MediaKind::Raw;
            entry.source = {0, static_cast<uint64_t>(i) * 4, 4};
            ctx.roots.push_back(std::move(entry));
        }
        return {true};
    }

private:
    StubOptions m_options;
    Types::TypeId m_blob;
};

}
