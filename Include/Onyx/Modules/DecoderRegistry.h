#pragma once
#include <Onyx/Modules/Workspace.h>      // DecodeContext needs Document bits
#include <Onyx/Parsers/SceneNode.h>
#include <Onyx/Parsers/TextureData.h>
#include <Onyx/Types/TypeId.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>

namespace Onyx::Modules {

struct DecodeContext {
    Document&              doc;           // tree, file, module state
    const Domain::AssetEntry& entry;      // the node being decoded
    Services::DiagSink&    diags;
    Services::Progress&    progress;
};

// Named DecodedText, not TextOut: on Windows <wingdi.h> defines TextOut as
// a macro (TextOutA/TextOutW), so a consumer who reached this type through
// <Onyx/Onyx.h> -- which pulls in <windows.h> via Services/PathUtils.h --
// got Onyx::Modules::TextOutA in their TU and Onyx::Modules::TextOut in the
// library, i.e. an LNK2019 on DecoderRegistry::Text/DecodeText. Found by
// the cold-start link exam (Tools/ColdStart) on its first run, at the
// v1.0.0 final review; renaming after the tag would have been MAJOR.
struct DecodedText { std::string text; std::string language; };  // language: "json", "lua", ""

using SceneDecoder = std::function<std::unique_ptr<Parsers::SceneData>(DecodeContext&)>;
using ImageDecoder = std::function<std::unique_ptr<Parsers::TextureData>(DecodeContext&)>;
using TextDecoder  = std::function<std::optional<DecodedText>(DecodeContext&)>;

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
    std::optional<DecodedText>                DecodeText (DecodeContext&) const;

    // Audio and Schema decoder slots arrive with their first consumer (deliberate YAGNI).

private:
    std::unordered_map<uint32_t, SceneDecoder> m_scenes;
    std::unordered_map<uint32_t, ImageDecoder> m_images;
    std::unordered_map<uint32_t, TextDecoder>  m_texts;
};

} // namespace Onyx::Modules
