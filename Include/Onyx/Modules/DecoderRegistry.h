#pragma once
#include <Onyx/Modules/Workspace.h>      // DecodeContext needs Document bits
#include <Onyx/Parsers/SceneNode.h>
#include <Onyx/Parsers/TextureData.h>
#include <Onyx/Types/TypeId.h>
#include <functional>
#include <memory>
#include <optional>

namespace Onyx::Modules {

struct DecodeContext {
    Document&              doc;           // tree, file, module state
    const Domain::AssetEntry& entry;      // the node being decoded
    Services::DiagSink&    diags;
    Services::Progress&    progress;
};

struct TextOut { std::string text; std::string language; };  // language: "json", "lua", ""

using SceneDecoder = std::function<std::unique_ptr<Parsers::SceneData>(DecodeContext&)>;
using ImageDecoder = std::function<std::unique_ptr<Parsers::TextureData>(DecodeContext&)>;
using TextDecoder  = std::function<std::optional<TextOut>(DecodeContext&)>;

class DecoderRegistry {
public:
    void Scene(Types::TypeId, SceneDecoder);
    void Image(Types::TypeId, ImageDecoder);
    void Text (Types::TypeId, TextDecoder);

    bool HasScene(Types::TypeId) const;
    bool HasImage(Types::TypeId) const;
    bool HasText (Types::TypeId) const;

    // Null/empty result when no decoder or the decoder salvage-fails
    // (failure detail goes through ctx.diags, never an exception).
    // Note: double registration on the same TypeId replaces (last wins).
    std::unique_ptr<Parsers::SceneData>   DecodeScene(DecodeContext&) const;
    std::unique_ptr<Parsers::TextureData> DecodeImage(DecodeContext&) const;
    std::optional<TextOut>                DecodeText (DecodeContext&) const;

private:
    std::unordered_map<uint32_t, SceneDecoder> m_scenes;
    std::unordered_map<uint32_t, ImageDecoder> m_images;
    std::unordered_map<uint32_t, TextDecoder>  m_texts;
};

} // namespace Onyx::Modules
