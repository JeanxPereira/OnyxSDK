#pragma once
// Onyx SDK — umbrella public header.
//
// ── The inclusion rule ────────────────────────────────────────────────────
// A header is NAMED by this umbrella when BOTH of the following hold. Both
// are checkable against the tree, so this is a rule that predicts the list
// below rather than a description written after it. (The test decides what
// this file names DIRECTLY; a header reached transitively through one it
// names is in the closure either way.)
//
//   (1) LINKABLE. Everything the header declares ships in a target that
//       Onyx::Onyx itself links -- Onyx_Core, Onyx_Render, Onyx_Shell
//       (root CMakeLists.txt, `target_link_libraries(Onyx INTERFACE ...)`).
//       README's "Consuming Onyx" tells a consumer to link exactly that
//       aggregate, so a declaration handed over from here whose definition
//       ships somewhere else is not a free convenience -- it is an LNK2019
//       with a delay fuse, which is the same defect as the audit's
//       blocking gap G1 (a public header whose symbol shipped in no
//       library) seen from the umbrella's side. Public headers that fail
//       this test are not demoted: they move to a sibling umbrella that
//       names the one extra target to link -- <Onyx/Render.h>
//       (Onyx::Render), <Onyx/Media.h> (Onyx::Media), <Onyx/Exchange.h>
//       (Onyx::Exchange), <Onyx/CliRender.h> (Onyx::CliRender),
//       <Onyx/TestKit.h> (Onyx::TestKit).
//
//   (2) REQUIRED OF THE CONSUMER. Building a toolkit on Onyx cannot be
//       done without naming something the header declares, at one of the
//       two places the SDK hands the work over:
//         (a) driving an umbrella class through its public interface --
//             to construct it, to call it, to receive what it hands back,
//             or to subscribe to an event it fires (subscribing means
//             naming the event type). This is why
//             Viewers/DocumentWindow.h is named here, and with it
//             Services/EventManager.h and Services/Events.h:
//             DocumentWindow/Viewport3D post EventDocumentOpened/
//             EventAnimationLoaded through them (Source/Viewers/
//             DocumentWindow.cpp, Source/Viewers/Viewport3D.cpp), and
//             withholding that catalog would leave a consumer unable to
//             subscribe to events the umbrella's own pipeline fires.
//         (b) implementing what the SDK asks the consumer to implement,
//             and booting it: an IGameModule with its parsers and
//             decoders, and the composition root that starts the app or
//             the CLI. Onyx cannot perform either on the consumer's
//             behalf -- only the module knows a chunk of bytes is
//             IMA-ADPCM audio (Audio/AdpcmDecoder.h, named here although
//             no umbrella class ever hands one back or calls one: the SDK
//             has no call site for it at all), and only the consumer's
//             own main() calls Onyx::Cli::Run (Cli/Commands.h).
//
//       "Required", not "merely useful". A header the Shell or the
//       renderer calls INTO on its own behalf fails (2) even when a
//       consumer could find a use for it -- the consumer never has to
//       write that name.
//
//       Honest about the two clauses' different strengths, because an
//       earlier version of this comment claimed more than it delivered and
//       a reviewer caught it: (1) is a PREDICATE -- mechanically checkable,
//       and checked (54 direct includes, every one shipping in Core/Render/
//       Shell, zero violations across the 82-header closure). (2) is a
//       JUDGEMENT that the list obeys in spirit but not as a decision
//       procedure: Rendering/AxisGizmo.h, Services/Metrics.h,
//       Vfs/TransformFile.h and Platform/SystemTheme.h are named here and
//       would not survive a literal "cannot be built without naming it"
//       test. They are harmless -- all four pass (1), so nothing here can
//       hand a consumer a link error -- but a reader should know that (1)
//       is what makes this list SAFE and (2) is only what makes it
//       CURATED. If the two ever conflict, (1) wins.
//
// ── What that rule excludes, and where it lives instead ───────────────────
//   - Shell-INTERNAL detail headers: App/Widgets.h, App/UIHelpers.h,
//     App/Formatting.h, App/TypeVisuals.h, App/TexturePool.h,
//     App/StatusBarFormat.h, App/InfoTab*.h, Fonts/IconTable.h and every
//     App/Panels/* header. These are how the Shell draws the panels IT
//     owns; a consumer registers an IPanel/IDocumentContent and never
//     writes those names. They stay reachable through their own
//     `#include <Onyx/Subsystem/Header.h>` for an author who does want to
//     reach into the Shell's toolbox -- same as any header not itemized
//     below.
//   - The Vulkan-touching half of the renderer (VkContext, Pipelines,
//     OffscreenTarget, RenderContext, SceneRendererVk, TexturePool,
//     VkResources). These pass (1) -- they ship in Onyx_Render -- and are
//     left out on a separate, explicitly-stated cost ground: every one of
//     them `#include`s volk.h/vk_mem_alloc.h directly or transitively, and
//     this umbrella is also included by headless/CLI-only consumers that
//     should never pay for Vulkan headers just to parse a container. They
//     live in the sibling <Onyx/Render.h> -- see its own top comment.
//     Rendering/RenderToImage.h is the one exception: it is deliberately
//     Vulkan-header-free (it forward-declares VkContext only -- see its
//     own top comment), so the "ready floor" one-call render entry point
//     ships in THIS header, not the sibling.
//   - Viewers/VideoPlayer.h, on the same cost ground plus a configuration
//     one: unlike every other viewer, its header (not just its .cpp)
//     directly #includes libavformat/libavcodec/libswscale/libswresample
//     and miniaudio.h, and ONYX_COMPONENT_MEDIA (root CMakeLists.txt) can
//     build Onyx WITHOUT FFmpeg at all -- unconditionally including it
//     would break `#include <Onyx/Onyx.h>` itself for that configuration.
//     It lives in the sibling <Onyx/Media.h>.
//   - Everything Onyx::Onyx does not link, by clause (1):
//     Exchange/GltfExport.h -> <Onyx/Exchange.h> (link Onyx::Exchange),
//     Cli/Render.h -> <Onyx/CliRender.h> (link Onyx::CliRender),
//     TestKit/*.h -> <Onyx/TestKit.h> (link Onyx::TestKit).
//     Through v0.6.0 this file named all five of those headers, arguing
//     the declarations were "free" without the link. They are not free:
//     they compile and then fail at link the moment a consumer calls one,
//     which is exactly what the umbrella promises will not happen.
//     Cli/Gltf.h is out for a stricter form of the same reason -- its
//     MakeGltfExportFn is compiled into the example CLI executable, so it
//     ships in no library at all (recorded in CHANGELOG.md's post-v1
//     backlog).
//
// Apps may include this, plus <Onyx/Render.h> for the raw Vulkan floor and
// <Onyx/Media.h> for the video viewer, for the full Onyx::Onyx-linkable
// surface -- or include individual <Onyx/Subsystem/Header.h> directly.
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

// CLI (Onyx::Cli::Run/CmdProbe/CmdList/CmdExtract/CmdDecode) -- the argv
// dispatcher a consumer's own main() calls. `render` is the one subcommand
// whose implementation ships outside Onyx::Onyx (Onyx_CliRender): its
// CmdRender declaration lives in the sibling <Onyx/CliRender.h>, and Run()
// reaches it through the RenderFn hook Commands.h declares here.
#include <Onyx/Cli/Commands.h>

