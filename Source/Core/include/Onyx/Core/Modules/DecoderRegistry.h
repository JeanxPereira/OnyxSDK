#pragma once

#include <Onyx/Core/Domain/Audio.h>
#include <Onyx/Core/Domain/Scene.h>
#include <Onyx/Core/Domain/Table.h>
#include <Onyx/Core/Domain/Text.h>
#include <Onyx/Core/Domain/Texture.h>
#include <Onyx/Core/Modules/Capabilities.h>
#include <Onyx/Core/Modules/DecodeContext.h>
#include <Onyx/Core/Schema/Schema.h>
#include <Onyx/Core/Types/TypeId.h>

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Onyx::Modules {

using SceneDecoder = std::function<std::unique_ptr<Domain::Scene>(DecodeContext&)>;
using ImageDecoder = std::function<std::optional<Domain::Texture>(DecodeContext&)>;
using TextDecoder = std::function<std::optional<Domain::Text>(DecodeContext&)>;
using AudioDecoder = std::function<std::optional<Domain::Audio>(DecodeContext&)>;
using TableDecoder = std::function<std::optional<Domain::Table>(DecodeContext&)>;
using AnyDecoder = std::function<std::shared_ptr<void>(DecodeContext&)>;

// Registration and decoding may run on different threads. A decoder that throws is reported as one
// Error diagnostic "onyx.decode.threw" and the decode yields null/nullopt. Registering the same
// slot for the same type again replaces the previous decoder.
class DecoderRegistry {
public:
    void Scene(Types::TypeId, SceneDecoder);
    void Image(Types::TypeId, ImageDecoder);
    void Text(Types::TypeId, TextDecoder);
    void Audio(Types::TypeId, AudioDecoder);
    void Table(Types::TypeId, TableDecoder);
    void Any(Types::TypeId, std::string capability, AnyDecoder);
    void Schema(Types::TypeId, std::shared_ptr<const Onyx::Schema::Schema>);

    Capabilities Of(Types::TypeId) const;

    std::unique_ptr<Domain::Scene> DecodeScene(DecodeContext&) const;
    std::optional<Domain::Texture> DecodeImage(DecodeContext&) const;
    std::optional<Domain::Text> DecodeText(DecodeContext&) const;
    std::optional<Domain::Audio> DecodeAudio(DecodeContext&) const;
    std::optional<Domain::Table> DecodeTable(DecodeContext&) const;
    std::shared_ptr<void> DecodeAny(DecodeContext&, std::string_view capability) const;
    std::shared_ptr<const Onyx::Schema::Schema> SchemaOf(Types::TypeId) const;

    void RemoveAll(std::span<const Types::TypeId> ids);

private:
    template <class Slot>
    using SlotMap = std::unordered_map<Types::TypeId, Slot>;

    mutable std::mutex m_mutex;
    SlotMap<SceneDecoder> m_scenes;
    SlotMap<ImageDecoder> m_images;
    SlotMap<TextDecoder> m_texts;
    SlotMap<AudioDecoder> m_audios;
    SlotMap<TableDecoder> m_tables;
    SlotMap<std::map<std::string, AnyDecoder, std::less<>>> m_any;
    SlotMap<std::shared_ptr<const Onyx::Schema::Schema>> m_schemas;
};

}
