#pragma once

// Shell glue that turns a SelectionChanged (double-click) into a decode +
// an opened viewer (M3b Task 4). Decoding runs on the caller's thread --
// today that is always the main/UI thread, since double-click handling
// lives in DocumentBrowser::Draw(); async decode is a recorded follow-up
// (see task-4-brief.md).
//
// Viewer construction is injected via ViewerOpener rather than done here
// directly, so OpenSelection stays testable without linking ImGui/GL: a
// test can supply no-op (or recording) callbacks and assert purely on the
// returned ViewerKind and on what Decode* produced, without ever
// constructing a real ImageViewer/TextEditorViewer/Viewport3D. The Shell
// (DocumentBrowser) supplies the real callbacks that call
// Api::Documents().AddTab(...).

#include <Onyx/App/ViewerRouting.h>
#include <Onyx/Modules/DecoderRegistry.h>
#include <Onyx/Modules/Selection.h>
#include <Onyx/Parsers/SceneNode.h>
#include <Onyx/Parsers/TextureData.h>

#include <functional>
#include <memory>
#include <string>

namespace Onyx::App {

// Any callback left unset is simply not invoked (e.g. a test that only
// cares about the Text path can leave openImage/openScene empty).
struct ViewerOpener {
    std::function<void(std::string name, std::unique_ptr<Onyx::Parsers::TextureData>)> openImage;
    std::function<void(std::string name, Onyx::Modules::TextOut)>                      openText;
    std::function<void(std::string name, std::unique_ptr<Onyx::Parsers::SceneData>)>   openScene;
};

// Resolves `sel.path` against `sel.doc`, decodes it on the caller's
// thread, and invokes the matching `opener` callback. Returns the
// ViewerKind that WAS opened (or attempted):
//   - the document is not ready (still mid-parse)              -> None, no callback
//   - the path does not resolve (stale/out-of-range)          -> None, no callback
//   - the resolved entry is Failed                           -> None, no callback,
//     logs the document's error diag count instead of decoding
//   - RouteForType finds no capability for the entry's type   -> None, no callback,
//     logs "no viewer for <type key>"
//   - a capability exists but Decode* salvage-fails (returns
//     null/empty; the decoder already reported into doc.diags) -> None, no callback
//   - otherwise                                                -> Scene/Image/Text,
//     the matching opener callback is invoked (if set)
ViewerKind OpenSelection(Onyx::Modules::Workspace& ws, const Onyx::Modules::SelectionChanged& sel,
                          const ViewerOpener& opener);

} // namespace Onyx::App
