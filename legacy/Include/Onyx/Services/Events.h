#pragma once

// Onyx Event Catalog
// All events used across the application.
// Convention: Event names match the subsystem they belong to.

#include <Onyx/Services/EventManager.h>

// Forward declarations
#include <memory>
namespace Onyx::Viewers { class IDocumentContent; }
namespace Onyx::Parsers { struct AnimationData; }
namespace Onyx::Services { struct AppConfig; }

// ── Lifecycle Events ───────────────────────────────────────────────────────

/// Fired after App::init() completes and all panels are registered.
EVENT_DEF(EventStartupFinished);

/// Fired when the application is about to close.
EVENT_DEF(EventShutdown);

// ── Document / Animation Events ───────────────────────────────────────────
// The profile-era WAD/ISO/asset-selection events (EventWadOpened,
// EventWadClosed, EventPakOpened, EventAllClosed, EventAssetSelected,
// EventAssetLoaded) were retired in M3b Task 6 along with IAssetProfile,
// ProfileManager, and AssetDatabase -- documents open through GameModules
// and the Workspace's own EventBus (DocumentOpened/TreeReady/DocumentClosed/
// SelectionChanged, see Include/Onyx/Modules/Workspace.h and Selection.h)
// now. The two events below are the viewer-tab flow, not the asset model,
// and stay.

/// Fired when a new document/viewer tab is opened.
/// @param IDocumentContent* the opened document
EVENT_DEF(EventDocumentOpened, Onyx::Viewers::IDocumentContent*);

/// Fired when animation data is loaded into a scene (e.g. Viewport3D).
/// @param std::shared_ptr<Onyx::Parsers::AnimationData> the loaded animation data
EVENT_DEF(EventAnimationLoaded, std::shared_ptr<Onyx::Parsers::AnimationData>);

// ── UI State Events ───────────────────────────────────────────────────────

/// Per-frame tick — subscribers that need continuous updates (animations, progress).
/// Posted near the TOP of the frame, BEFORE panels/documents draw.
/// Marked NO_LOG to avoid spamming the debug output.
EVENT_DEF_NO_LOG(EventFrameTick);

/// End-of-frame tick — posted AFTER all panels and documents have drawn for the
/// frame. Subscribers that must observe per-frame UI mutations (e.g. mirroring a
/// widget value back to config the same frame the user changed it) belong here.
/// Marked NO_LOG to avoid spamming the debug output.
EVENT_DEF_NO_LOG(EventFrameEnd);

/// Fired when the AppConfig is modified (settings changed).
EVENT_DEF(EventConfigChanged);
