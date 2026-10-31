#pragma once

#include <Onyx/Core/Domain/Audio.h>
#include <Onyx/Core/Domain/MediaKind.h>
#include <Onyx/Core/Domain/Scene.h>
#include <Onyx/Core/Domain/Table.h>
#include <Onyx/Core/Domain/Text.h>
#include <Onyx/Core/Domain/Texture.h>
#include <Onyx/Core/Modules/DecoderRegistry.h>
#include <Onyx/Core/Modules/Workspace.h>
#include <Onyx/Core/Types/TypeCatalog.h>
#include <Onyx/Ui/Panel.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace Onyx::Ui {

struct OpenRequest {
    Modules::Workspace& workspace;
    Modules::DocumentId doc;
    Modules::NodePath node;
    Types::TypeId type;
    Domain::MediaKind kind;
    std::string name;
    std::optional<Domain::Texture> image;
    std::optional<Domain::Text> text;
    std::unique_ptr<Domain::Scene> scene;
    std::optional<Domain::Audio> audio;
    std::optional<Domain::Table> table;
    std::vector<std::byte> raw;
};

struct ViewerSpec {
    std::string id;
    std::variant<Domain::MediaKind, Types::TypeKey> target;
    int priority = 0;
    std::function<std::unique_ptr<IDocumentView>(OpenRequest&)> create;
};

class ViewerRegistry {
public:
    // Replaces a spec with the same id, keeping its position.
    void Register(ViewerSpec spec);
    void Unregister(std::string_view id);

    // A TypeKey target equal to the type's key beats any MediaKind target; ties go to the higher
    // priority, then the earlier registration. A MediaKind target applies when the type's media
    // matches and its decoder slot exists (Raw and Unknown need no decoder). Null when none applies.
    const ViewerSpec* Route(const Types::TypeCatalog& catalog, const Modules::DecoderRegistry& decoders,
                            Types::TypeId type) const;

    std::span<const ViewerSpec> All() const { return m_specs; }

private:
    std::vector<ViewerSpec> m_specs;
};

}
