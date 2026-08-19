#pragma once
// Onyx SDK — umbrella public header.
//
// Inclusion rule (audit gap G2 fix): this header pulls in every header that
// declares a public ENTRY POINT a toolkit author is expected to call
// directly to build a working toolkit on Onyx -- boot the app, register a
// module, parse/decode/browse assets, drive the document/viewer/selection
// pipeline, theme the UI, and consume the renderer's header-only ("ready
// floor") surface. Concretely, that means: if a class whose header is
// already in this umbrella calls a function or posts an event declared in
// some OTHER header, that other header belongs here too -- a consumer who
// can construct/subscribe-to the umbrella's own classes must not have to
// leave the umbrella to fully use them. Services/EventManager.h and
// Services/Events.h are included on exactly this basis: Viewers/
// DocumentWindow.h and Viewers/Viewport3D.h (both already below) post
// EventDocumentOpened/EventAnimationLoaded through them
// (Source/Viewers/DocumentWindow.cpp, Source/Viewers/Viewport3D.cpp) --
// withholding the event catalog those umbrella-included classes actually
// use would leave a consumer unable to subscribe to events the umbrella's
// own document/viewer pipeline fires. (Services/EventBus.h -- a newer,
// non-singleton pub/sub EventManager's own header says it will eventually
// replace -- is already transitively reachable via App/Window.h and
// Modules/Workspace.h, both below; EventManager/Events.h stay because they
// are what DocumentWindow/Viewport3D actually call TODAY, not instead of
// EventBus.)
//
// It deliberately excludes:
//
//   - Shell-INTERNAL detail headers a toolkit author has no reason to name
//     directly: individual App::Panels/*, low-level UI helper headers
//     (Formatting, InfoTab*, StatusBarFormat, TypeVisuals, UIHelpers,
//     Widgets, TexturePool), icon/font tables. These stay reachable via
//     their own `#include <Onyx/Subsystem/Header.h>` for an author who does
//     want to reach into the Shell's own toolbox, same as any header not
//     itemized below.
//   - The Vulkan-touching half of the renderer (VkContext, Pipelines,
//     OffscreenTarget, RenderContext, SceneRendererVk, TexturePool,
//     VkResources): every one of those headers `#include`s volk.h/
//     vk_mem_alloc.h directly or transitively, and this umbrella is also
//     included by headless/CLI-only consumers that should never pay for
//     Vulkan headers just to parse a container. That half lives in the
//     sibling <Onyx/Render.h> instead -- see its own top comment.
//     Rendering/RenderToImage.h is the one exception: it is deliberately
//     Vulkan-header-free (forward-declares VkContext only -- see its own
//     top comment), so the "ready floor" one-call render entry point ships
//     in THIS header, not the sibling.
//   - Viewers/VideoPlayer.h: unlike every other viewer, its header (not
//     just its .cpp) directly #includes libavformat/libavcodec/libswscale/
//     libswresample and miniaudio.h. ONYX_COMPONENT_MEDIA (root
//     CMakeLists.txt) can build Onyx WITHOUT FFmpeg at all -- unconditionally
//     including this header would break `#include <Onyx/Onyx.h>` itself for
//     that configuration, not just cost extra compile time. It lives in the
//     sibling <Onyx/Media.h>, included only when ONYX_COMPONENT_MEDIA is on.
//
// Apps may include this, plus <Onyx/Render.h> for the raw Vulkan floor and
// <Onyx/Media.h> for the video viewer, for the full public surface -- or
// include individual <Onyx/Subsystem/Header.h> directly.
#include <Onyx/Version.h>

// Core data
#include <Onyx/Vfs/IFile.h>
#include <Onyx/Vfs/IsoFileSystem.h>
#include <Onyx/Vfs/OsFile.h>
#include <Onyx/Vfs/MemoryFile.h>
#include <Onyx/Vfs/SliceFile.h>
#include <Onyx/Vfs/TransformFile.h>
#include <Onyx/Schema/StructDef.h>
#include <Onyx/Schema/AssetReader.h>
#include <Onyx/Schema/AssetFormat.h>
#include <Onyx/Schema/NodeInstance.h>
#include <Onyx/Container/ChunkReader.h>
#include <Onyx/Container/ChunkSchema.h>
#include <Onyx/Container/ChunkTree.h>
#include <Onyx/Types/TypeId.h>
#include <Onyx/Types/TypeCatalog.h>
#include <Onyx/Domain/MediaKind.h>
#include <Onyx/Domain/Entry.h>
#include <Onyx/Domain/Wad.h>

// Format utilities a module author composes when parsing its own container
#include <Onyx/Audio/AdpcmDecoder.h>

// Services
#include <Onyx/Services/AppConfig.h>
#include <Onyx/Services/Logger.h>
#include <Onyx/Services/Threading.h>
#include <Onyx/Services/ThemeManager.h>
#include <Onyx/Services/Appearance.h>
#include <Onyx/Services/PathUtils.h>
#include <Onyx/Services/Metrics.h>
#include <Onyx/Services/AssetVisibility.h>
#include <Onyx/Services/FrameScheduler.h>
#include <Onyx/Services/TaskManager.h>
#include <Onyx/Services/EventManager.h>
#include <Onyx/Services/Events.h>
#include <Onyx/Platform/SystemTheme.h>
#include <Onyx/Api/ToolkitApi.h>

// Module contract (IGameModule's own surface plus the two pieces that were
// only forward-declared until this task: DecoderRegistry, Selection)
#include <Onyx/Modules/DecoderRegistry.h>
#include <Onyx/Modules/Selection.h>

// App shell + document/viewer pipeline
#include <Onyx/App/Window.h>
#include <Onyx/App/App.h>
#include <Onyx/App/IPanel.h>
#include <Onyx/App/ViewerRegistry.h>
#include <Onyx/App/ViewerOpening.h>
#include <Onyx/App/ViewerRouting.h>
#include <Onyx/Viewers/IDocumentContent.h>
#include <Onyx/Viewers/DocumentWindow.h>
#include <Onyx/Viewers/ImageViewer.h>
#include <Onyx/Viewers/TextEditorViewer.h>
#include <Onyx/Viewers/Viewport3D.h>
// Viewers/VideoPlayer.h is NOT here -- see this file's top comment; it lives
// in the sibling <Onyx/Media.h>, gated on ONYX_COMPONENT_MEDIA.

// Renderer -- the Vulkan-header-free half only (data types + the one-call
// ready-floor entry point). See this file's top comment and <Onyx/Render.h>
// for the raw Vulkan floor this deliberately leaves out.
#include <Onyx/Rendering/RenderBatch.h>
#include <Onyx/Rendering/JointPalette.h>
#include <Onyx/Rendering/Camera.h>
#include <Onyx/Rendering/AnimationPlayer.h>
#include <Onyx/Rendering/AxisGizmo.h>
#include <Onyx/Rendering/RenderToImage.h>

// Exchange (glTF export)
#include <Onyx/Exchange/GltfExport.h>

// CLI (Onyx::Cli::Run/CmdProbe/CmdList/CmdExtract/CmdDecode/CmdRender)
#include <Onyx/Cli/Commands.h>
#include <Onyx/Cli/Render.h>

// TestKit -- opt-in target (Onyx::TestKit), but the declarations are free to
// include here same as everything else: a consumer only pays for it by
// linking Onyx::TestKit, same as the CLI/Render headers above only cost
// anything once Onyx::CliRender is linked.
#include <Onyx/TestKit/DecodeSmoke.h>
#include <Onyx/TestKit/Goldens.h>
#include <Onyx/TestKit/RenderCompare.h>
