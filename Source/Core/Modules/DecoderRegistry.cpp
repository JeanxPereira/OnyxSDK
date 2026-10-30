#include <Onyx/Core/Modules/DecoderRegistry.h>

#include <exception>
#include <utility>

namespace Onyx::Modules {

namespace {

template <class Slot>
Slot Find(const std::unordered_map<Types::TypeId, Slot>& map, Types::TypeId id) {
    auto it = map.find(id);
    return it == map.end() ? Slot{} : it->second;
}

void ReportThrew(DecodeContext& ctx, std::string message) {
    ctx.diags.Report(Services::Diag{Services::Severity::Error, "onyx.decode.threw", std::move(message), std::nullopt});
}

template <class R, class F>
R Invoke(const F& decoder, DecodeContext& ctx) {
    if (!decoder) {
        return R{};
    }
    try {
        return decoder(ctx);
    } catch (const std::exception& e) {
        ReportThrew(ctx, e.what());
    } catch (...) {
        ReportThrew(ctx, "unknown exception");
    }
    return R{};
}

}

void DecoderRegistry::Scene(Types::TypeId id, SceneDecoder decoder) {
    std::lock_guard lock(m_mutex);
    m_scenes[id] = std::move(decoder);
}

void DecoderRegistry::Image(Types::TypeId id, ImageDecoder decoder) {
    std::lock_guard lock(m_mutex);
    m_images[id] = std::move(decoder);
}

void DecoderRegistry::Text(Types::TypeId id, TextDecoder decoder) {
    std::lock_guard lock(m_mutex);
    m_texts[id] = std::move(decoder);
}

void DecoderRegistry::Audio(Types::TypeId id, AudioDecoder decoder) {
    std::lock_guard lock(m_mutex);
    m_audios[id] = std::move(decoder);
}

void DecoderRegistry::Table(Types::TypeId id, TableDecoder decoder) {
    std::lock_guard lock(m_mutex);
    m_tables[id] = std::move(decoder);
}

void DecoderRegistry::Any(Types::TypeId id, std::string capability, AnyDecoder decoder) {
    std::lock_guard lock(m_mutex);
    m_any[id][std::move(capability)] = std::move(decoder);
}

void DecoderRegistry::Schema(Types::TypeId id, std::shared_ptr<const Onyx::Schema::Schema> schema) {
    std::lock_guard lock(m_mutex);
    m_schemas[id] = std::move(schema);
}

Capabilities DecoderRegistry::Of(Types::TypeId id) const {
    std::lock_guard lock(m_mutex);
    Capabilities caps;
    caps.scene = m_scenes.contains(id);
    caps.image = m_images.contains(id);
    caps.text = m_texts.contains(id);
    caps.audio = m_audios.contains(id);
    caps.table = m_tables.contains(id);
    caps.schema = m_schemas.contains(id);
    if (auto it = m_any.find(id); it != m_any.end()) {
        for (const auto& [name, decoder] : it->second) {
            caps.any.push_back(name);
        }
    }
    return caps;
}

std::unique_ptr<Domain::Scene> DecoderRegistry::DecodeScene(DecodeContext& ctx) const {
    SceneDecoder decoder;
    {
        std::lock_guard lock(m_mutex);
        decoder = Find(m_scenes, ctx.entry.type);
    }
    return Invoke<std::unique_ptr<Domain::Scene>>(decoder, ctx);
}

std::optional<Domain::Texture> DecoderRegistry::DecodeImage(DecodeContext& ctx) const {
    ImageDecoder decoder;
    {
        std::lock_guard lock(m_mutex);
        decoder = Find(m_images, ctx.entry.type);
    }
    return Invoke<std::optional<Domain::Texture>>(decoder, ctx);
}

std::optional<Domain::Text> DecoderRegistry::DecodeText(DecodeContext& ctx) const {
    TextDecoder decoder;
    {
        std::lock_guard lock(m_mutex);
        decoder = Find(m_texts, ctx.entry.type);
    }
    return Invoke<std::optional<Domain::Text>>(decoder, ctx);
}

std::optional<Domain::Audio> DecoderRegistry::DecodeAudio(DecodeContext& ctx) const {
    AudioDecoder decoder;
    {
        std::lock_guard lock(m_mutex);
        decoder = Find(m_audios, ctx.entry.type);
    }
    return Invoke<std::optional<Domain::Audio>>(decoder, ctx);
}

std::optional<Domain::Table> DecoderRegistry::DecodeTable(DecodeContext& ctx) const {
    TableDecoder decoder;
    {
        std::lock_guard lock(m_mutex);
        decoder = Find(m_tables, ctx.entry.type);
    }
    return Invoke<std::optional<Domain::Table>>(decoder, ctx);
}

std::shared_ptr<void> DecoderRegistry::DecodeAny(DecodeContext& ctx, std::string_view capability) const {
    AnyDecoder decoder;
    {
        std::lock_guard lock(m_mutex);
        if (auto it = m_any.find(ctx.entry.type); it != m_any.end()) {
            if (auto slot = it->second.find(capability); slot != it->second.end()) {
                decoder = slot->second;
            }
        }
    }
    return Invoke<std::shared_ptr<void>>(decoder, ctx);
}

std::shared_ptr<const Onyx::Schema::Schema> DecoderRegistry::SchemaOf(Types::TypeId id) const {
    std::lock_guard lock(m_mutex);
    return Find(m_schemas, id);
}

void DecoderRegistry::RemoveAll(std::span<const Types::TypeId> ids) {
    std::lock_guard lock(m_mutex);
    for (Types::TypeId id : ids) {
        m_scenes.erase(id);
        m_images.erase(id);
        m_texts.erase(id);
        m_audios.erase(id);
        m_tables.erase(id);
        m_any.erase(id);
        m_schemas.erase(id);
    }
}

}
