#include <Onyx/Modules/DecoderRegistry.h>
#include <exception>

namespace Onyx::Modules {

void DecoderRegistry::Scene(Types::TypeId typeId, SceneDecoder decoder) {
    m_scenes[typeId.value] = decoder;
}

void DecoderRegistry::Image(Types::TypeId typeId, ImageDecoder decoder) {
    m_images[typeId.value] = decoder;
}

void DecoderRegistry::Text(Types::TypeId typeId, TextDecoder decoder) {
    m_texts[typeId.value] = decoder;
}

bool DecoderRegistry::HasScene(Types::TypeId typeId) const {
    return m_scenes.find(typeId.value) != m_scenes.end();
}

bool DecoderRegistry::HasImage(Types::TypeId typeId) const {
    return m_images.find(typeId.value) != m_images.end();
}

bool DecoderRegistry::HasText(Types::TypeId typeId) const {
    return m_texts.find(typeId.value) != m_texts.end();
}

std::unique_ptr<Parsers::SceneData> DecoderRegistry::DecodeScene(DecodeContext& ctx) const {
    auto it = m_scenes.find(ctx.entry.typeId.value);
    if (it == m_scenes.end()) return nullptr;

    try {
        return it->second(ctx);
    } catch (const std::exception& e) {
        ctx.diags.Report(Services::Diag{
            Services::Severity::Error,
            "decoder.threw",
            e.what(),
            std::nullopt});
        return nullptr;
    } catch (...) {
        ctx.diags.Report(Services::Diag{
            Services::Severity::Error,
            "decoder.threw",
            "decoder threw",
            std::nullopt});
        return nullptr;
    }
}

std::unique_ptr<Parsers::TextureData> DecoderRegistry::DecodeImage(DecodeContext& ctx) const {
    auto it = m_images.find(ctx.entry.typeId.value);
    if (it == m_images.end()) return nullptr;

    try {
        return it->second(ctx);
    } catch (const std::exception& e) {
        ctx.diags.Report(Services::Diag{
            Services::Severity::Error,
            "decoder.threw",
            e.what(),
            std::nullopt});
        return nullptr;
    } catch (...) {
        ctx.diags.Report(Services::Diag{
            Services::Severity::Error,
            "decoder.threw",
            "decoder threw",
            std::nullopt});
        return nullptr;
    }
}

std::optional<TextOut> DecoderRegistry::DecodeText(DecodeContext& ctx) const {
    auto it = m_texts.find(ctx.entry.typeId.value);
    if (it == m_texts.end()) return std::nullopt;

    try {
        return it->second(ctx);
    } catch (const std::exception& e) {
        ctx.diags.Report(Services::Diag{
            Services::Severity::Error,
            "decoder.threw",
            e.what(),
            std::nullopt});
        return std::nullopt;
    } catch (...) {
        ctx.diags.Report(Services::Diag{
            Services::Severity::Error,
            "decoder.threw",
            "decoder threw",
            std::nullopt});
        return std::nullopt;
    }
}

} // namespace Onyx::Modules
