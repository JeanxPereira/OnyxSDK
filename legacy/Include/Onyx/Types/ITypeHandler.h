#pragma once
#include <Onyx/Parsers/SceneNode.h>
#include <Onyx/Vfs/IFile.h>
#include <Onyx/Types/TypeId.h>
#include <Onyx/Fonts/SFSymbols.h>
#include <cstddef>
#include <cstdint>
#include <memory>

// Forward declarations
namespace Onyx::Schema { class AssetNode; }
namespace Onyx::Viewers { class IDocumentContent; }
namespace Onyx::Parsers { struct SceneData; }
namespace Onyx::Domain { struct AssetEntry; struct AssetContainer; }

namespace Onyx::Types {

/// Abstract handler for a single asset type: identify, parse, visualize.
class ITypeHandler {
public:
  virtual ~ITypeHandler() = default;

  /// Unique type identity (minted in the TypeCatalog).
  virtual TypeId GetId() const = 0;

  /// Human-readable name (e.g. "Model", "Material").
  virtual const char *GetName() const = 0;

  /// Icon string for the tree view (UTF-8 codicon). Override to customize.
  virtual const char *GetIcon() const { return ICON_SF_DOCUMENT; }

  /// RGBA color for the tree view label. Override to customize.
  struct Color4f { float r, g, b, a; };
  virtual Color4f GetColor() const { return {0.6f, 0.6f, 0.6f, 1.0f}; }

  /// Parse raw payload bytes into a structured AssetNode tree for InfoTab.
  virtual std::shared_ptr<Schema::AssetNode> Parse(std::shared_ptr<Vfs::IFile> file) {
    (void)file; return nullptr;
  }

  /// Create a viewer for the given entry. Return nullptr if none.
  virtual std::shared_ptr<Viewers::IDocumentContent>
  CreateViewer(const Domain::AssetEntry &entry, Domain::AssetContainer &wad) {
    (void)entry; (void)wad; return nullptr;
  }

  /// Extract scene data without generating a viewer.
  virtual std::unique_ptr<Onyx::Parsers::SceneData>
  BuildSceneData(const Domain::AssetEntry &entry, Domain::AssetContainer &wad) {
    (void)entry; (void)wad; return nullptr;
  }
};

} // namespace Onyx::Types

// ── File-level self-registration (identified by extension, not magic) ────────
// Usage at the bottom of a handler .cpp:
//   ONYX_REGISTER_FILE_TYPE(VagAudioHandler);
//
// Named ONYX_*, not the GOW_*/underscore-prefixed form this macro carried
// through v0.6.0, for two reasons that are both v1.0-or-never under the
// stability policy in README.md: a game's name has no business on the main
// registration macro of a deliberately game-agnostic SDK (spec 13's intent,
// reaching the one place its letter -- Core/Render headers -- did not), and
// an identifier beginning with an underscore followed by a capital letter
// is reserved to the implementation in every scope, so the old spelling was
// undefined behaviour a conforming standard library was free to break.
#define ONYX_REG_CONCAT2(a, b) a##b
#define ONYX_REG_CONCAT(a, b) ONYX_REG_CONCAT2(a, b)

#define ONYX_REGISTER_FILE_TYPE(HandlerClass)                                       \
  static bool ONYX_REG_CONCAT(onyx_reg_ft_##HandlerClass##_, __LINE__) = [] {      \
    ::Onyx::Types::TypeRegistry::Get().RegisterByTypeId(                       \
        std::make_unique<HandlerClass>());                                     \
    return true;                                                               \
  }()

// Forward-declare TypeRegistry so the macro compiles where included first.
namespace Onyx::Types { class TypeRegistry; }
