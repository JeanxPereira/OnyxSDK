#pragma once

// Shell glue that turns a SelectionChanged (double-click) into a decode +
// an opened viewer (M3b Task 4). Task 13 (M4) moved the decode itself off
// the caller's thread: OpenSelection still resolves/routes synchronously
// (that part is cheap -- a tree walk and a capability lookup, no I/O), but
// the actual DecodeScene/DecodeImage/DecodeText call now runs on the
// Workspace's own JobQueue, on the `kDecodeLane` lane, completing only
// when the caller later Pump()s that queue -- exactly the M2 JobQueue
// contract (Include/Onyx/Services/Jobs.h): lane-serialized, Done only runs
// from Pump(), on the Pump()-calling thread.
//
// Viewer construction is injected via ViewerOpener rather than done here
// directly, so OpenSelection stays testable without linking ImGui/GL: a
// test can supply no-op (or recording) callbacks and assert purely on the
// returned ViewerKind and on what a decode produced once the job queues
// are pumped, without ever constructing a real ImageViewer/
// TextEditorViewer/Viewport3D. The Shell (DocumentBrowser) supplies the
// real callbacks that call Api::Documents().AddTab(...).
//
// Cancellation: OpenSelection subscribes itself, internally, to the
// Workspace's DocumentClosed event for the exact document the decode
// belongs to -- the subscription lives exactly as long as the submitted
// job does (it is released the instant that job's Done callback runs, one
// way or another) and its only job is to call the job's own
// JobHandle::Cancel() if DocumentClosed fires first. This is the
// "cooperative" half of the M2 contract: Cancel() only sets a flag a
// well-behaved decoder polls via Progress::CancelRequested() -- it does
// not itself stop anything. The correctness backstop that does not depend
// on any decoder honoring that flag is Done() re-checking, by id, that the
// document is still open (Workspace::Get) before ever calling an opener --
// the same "was this Closed out from under the async work" pattern
// Workspace::OpenAsync's own Done callback already uses for TreeReady.

#include <Onyx/App/ViewerRouting.h>
#include <Onyx/Modules/DecoderRegistry.h>
#include <Onyx/Modules/Selection.h>
#include <Onyx/Parsers/SceneNode.h>
#include <Onyx/Parsers/TextureData.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace Onyx::App {

// The lane every OpenSelection decode job is submitted on (Task 13b).
// Deliberately a single shared lane, not one per document like the
// Workspace's own parse lane (Workspace.h: lane == DocumentId): decode
// jobs across every open document serialize against each other on this
// one lane, which is the simplest thing that is still correct (avoids a
// second per-document lane-numbering scheme colliding with parse's). 0 is
// safe to use here because it is Modules::DocumentId's own reserved
// "invalid" value (Workspace.h: "0 = invalid") -- a real parse lane, which
// is always some document's id, can never be 0.
constexpr uint64_t kDecodeLane = 0;

// Any callback left unset is simply not invoked (e.g. a test that only
// cares about the Text path can leave openImage/openScene empty).
struct ViewerOpener {
    // `doc` is the SelectionChanged's DocumentId the newly decoded content
    // belongs to -- pass it straight through to
    // DocumentWindow::AddTab(tab, doc) so the (DocumentId, tab)
    // association (Task 13a) exists from the moment the tab appears.
    std::function<void(Onyx::Modules::DocumentId doc, std::string name,
                        std::unique_ptr<Onyx::Parsers::TextureData>)> openImage;
    std::function<void(Onyx::Modules::DocumentId doc, std::string name,
                        Onyx::Modules::TextOut)>                      openText;
    std::function<void(Onyx::Modules::DocumentId doc, std::string name,
                        std::unique_ptr<Onyx::Parsers::SceneData>)>   openScene;

    // Decode now runs on a background job (Task 13b); these two bracket
    // it on the UI side.
    //
    // openPlaceholder is called synchronously, on OpenSelection's own
    // caller thread, the instant a decode capability is found and the job
    // is about to be submitted -- before OpenSelection returns. It may add
    // a "Decoding..." tab (associated with `doc`, so a Close() of that
    // document during the decode closes the placeholder too, via the
    // ordinary (DocumentId, tab) close path) and return an opaque handle.
    // OpenSelection never interprets that handle -- it only ever hands it
    // back, unchanged, to closePlaceholder exactly once, when the job's
    // Done callback runs (whether the decode actually succeeded,
    // salvage-failed, or the document was closed out from under it).
    // Leaving either unset makes that half a no-op -- a test that does not
    // care about the placeholder can ignore both.
    std::function<std::shared_ptr<void>(Onyx::Modules::DocumentId doc, std::string name)> openPlaceholder;
    std::function<void(std::shared_ptr<void> placeholder)>                                closePlaceholder;
};

// Resolves `sel.path` against `sel.doc` and routes it, on the caller's
// thread (cheap: no decode happens here). Returns the ViewerKind that WAS
// routed (or attempted):
//   - the document is not ready (still mid-parse)              -> None, nothing submitted
//   - the path does not resolve (stale/out-of-range)          -> None, nothing submitted
//   - the resolved entry is Failed                           -> None, nothing submitted,
//     logs the document's error diag count instead of decoding
//   - RouteForType finds no capability for the entry's type   -> None, nothing submitted,
//     logs "no viewer for <type key>"
//   - otherwise                                                -> Scene/Image/Text:
//     opener.openPlaceholder is invoked (if set), then the actual decode
//     is submitted to the Workspace's JobQueue on kDecodeLane. Once that
//     job completes AND the caller has Pump()'d the queue,
//     opener.closePlaceholder runs, and then -- only if the document is
//     still open and the decode salvage-succeeded (did not return
//     null/empty; the decoder already reported into doc.diags on failure)
//     -- the matching opener.openScene/openImage/openText callback runs.
//     A salvage-failed decode logs the same LOG_WARN a synchronous
//     failure used to, from inside that Done callback instead of from
//     OpenSelection itself.
ViewerKind OpenSelection(Onyx::Modules::Workspace& ws, const Onyx::Modules::SelectionChanged& sel,
                          const ViewerOpener& opener);

} // namespace Onyx::App
