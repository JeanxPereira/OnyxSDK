#include <Onyx/App/ViewerOpening.h>

#include <Onyx/Modules/Workspace.h>
#include <Onyx/Services/EventBus.h>
#include <Onyx/Services/Jobs.h>
#include <Onyx/Services/Logger.h>
#include <Onyx/Types/TypeCatalog.h>

#include <utility>

namespace Onyx::App {

using Onyx::Domain::AssetEntry;
using Onyx::Domain::NodeFlags;
using Onyx::Modules::DecodeContext;
using Onyx::Modules::Document;
using Onyx::Modules::DocumentClosed;
using Onyx::Modules::DocumentId;
using Onyx::Modules::SelectionChanged;
using Onyx::Modules::Workspace;
using Onyx::Services::Progress;
using Onyx::Services::Severity;
using Onyx::Services::Subscription;

namespace {

// Workspace::Get() only ever hands back a raw Document*, which is no good
// for a job whose Work callback runs on a worker thread well after
// OpenSelection returns -- the same Close()-vs-in-flight-parse hazard
// PrepareDocument/OpenAsync guard against for parsing (Workspace.cpp), and
// the same fix: find the shared_ptr<Document> Workspace itself owns (via
// Documents(), which is exactly what m_documents holds) and capture THAT
// by value, so the Document object stays alive for the worker even if
// Close(id) erases Workspace's own entry while the job is in flight.
std::shared_ptr<Document> FindShared(Workspace& ws, DocumentId id) {
    for (const auto& d : ws.Documents()) {
        if (d->id == id) return d;
    }
    return nullptr;
}

void LogDecodeFailed(Workspace& ws, const AssetEntry& entry) {
    ONYX_LOGF_WARN("[Viewer] decode failed for '%s' (%s) - see diagnostics", entry.name.c_str(),
             std::string(ws.Catalog().KeyOf(entry.typeId)).c_str());
}

} // namespace

ViewerKind OpenSelection(Workspace& ws, const SelectionChanged& sel, const ViewerOpener& opener) {
    Document* doc = ws.Get(sel.doc);
    if (!doc) return ViewerKind::None;

    // Thread rule (Workspace.h): a document's roots are unsafe to resolve
    // against before its parse finishes -- `ready` is the only field safe
    // to poll on a document that might still be mid-parse on a worker
    // thread (same guard as InfoTab::Draw()).
    if (!doc->ready.load()) return ViewerKind::None;

    const AssetEntry* entry = Onyx::Modules::Resolve(*doc, sel.path);
    if (!entry) return ViewerKind::None;

    if ((static_cast<uint8_t>(entry->flags) & static_cast<uint8_t>(NodeFlags::Failed)) != 0) {
        ONYX_LOGF_WARN("%s: not decoding a Failed node (%zu error diag(s))", entry->name.c_str(),
                 doc->diags.Count(Severity::Error));
        return ViewerKind::None;
    }

    const ViewerKind kind = RouteForType(ws.Decoders(), entry->typeId);
    if (kind == ViewerKind::None) {
        ONYX_LOGF_INFO("no viewer for %s", std::string(ws.Catalog().KeyOf(entry->typeId)).c_str());
        return ViewerKind::None;
    }

    // Everything from here on is committed to a background decode -- keep
    // the Document object alive for the worker regardless of what happens
    // to Workspace's own bookkeeping while the job is in flight.
    std::shared_ptr<Document> docShared = FindShared(ws, sel.doc);
    if (!docShared) return ViewerKind::None; // should never happen: doc came from this same Get()

    const DocumentId docId = sel.doc;
    const std::string name = entry->name;

    std::shared_ptr<void> placeholder;
    if (opener.openPlaceholder) placeholder = opener.openPlaceholder(docId, name);

    // Cancel-on-close subscription for the job about to be submitted.
    // Default-constructed (empty/no-op) for now -- assigned its real
    // registration right after Submit() hands back the JobHandle to
    // cancel (chicken-and-egg: the subscription's handler needs the
    // handle, which does not exist until Submit() returns). The Done
    // callback below captures this SAME shared_ptr and resets it the
    // instant the job finishes, one way or another -- unsubscribing
    // immediately rather than waiting on JobState's own destruction, and
    // making a Cancel() call after that point a guaranteed no-op.
    auto subBox = std::make_shared<Subscription>();

    switch (kind) {
    case ViewerKind::Scene: {
        auto result = std::make_shared<std::unique_ptr<Onyx::Parsers::SceneData>>();
        Services::JobHandle handle = ws.Jobs().Submit(
            kDecodeLane,
            [&ws, docShared, entry, result](Progress& progress) {
                DecodeContext ctx{*docShared, *entry, docShared->diags, progress};
                *result = ws.Decoders().DecodeScene(ctx);
            },
            [&ws, docShared, docId, name, entry, placeholder, opener, result, subBox]() {
                *subBox = Subscription(); // unsubscribe: this job is done either way
                if (opener.closePlaceholder) opener.closePlaceholder(placeholder);
                if (!ws.Get(docId)) return; // document Closed while decoding
                if (!*result) { LogDecodeFailed(ws, *entry); return; }
                if (opener.openScene) opener.openScene(docId, name, std::move(*result));
            });
        *subBox = ws.Events().On<DocumentClosed>([docId, handle](const DocumentClosed& ev) mutable {
            if (ev.id == docId) handle.Cancel();
        });
        return ViewerKind::Scene;
    }
    case ViewerKind::Image: {
        auto result = std::make_shared<std::unique_ptr<Onyx::Parsers::TextureData>>();
        Services::JobHandle handle = ws.Jobs().Submit(
            kDecodeLane,
            [&ws, docShared, entry, result](Progress& progress) {
                DecodeContext ctx{*docShared, *entry, docShared->diags, progress};
                *result = ws.Decoders().DecodeImage(ctx);
            },
            [&ws, docShared, docId, name, entry, placeholder, opener, result, subBox]() {
                *subBox = Subscription();
                if (opener.closePlaceholder) opener.closePlaceholder(placeholder);
                if (!ws.Get(docId)) return;
                if (!*result) { LogDecodeFailed(ws, *entry); return; }
                if (opener.openImage) opener.openImage(docId, name, std::move(*result));
            });
        *subBox = ws.Events().On<DocumentClosed>([docId, handle](const DocumentClosed& ev) mutable {
            if (ev.id == docId) handle.Cancel();
        });
        return ViewerKind::Image;
    }
    case ViewerKind::Text: {
        auto result = std::make_shared<std::optional<Onyx::Modules::DecodedText>>();
        Services::JobHandle handle = ws.Jobs().Submit(
            kDecodeLane,
            [&ws, docShared, entry, result](Progress& progress) {
                DecodeContext ctx{*docShared, *entry, docShared->diags, progress};
                *result = ws.Decoders().DecodeText(ctx);
            },
            [&ws, docShared, docId, name, entry, placeholder, opener, result, subBox]() {
                *subBox = Subscription();
                if (opener.closePlaceholder) opener.closePlaceholder(placeholder);
                if (!ws.Get(docId)) return;
                if (!*result) { LogDecodeFailed(ws, *entry); return; }
                if (opener.openText) opener.openText(docId, name, std::move(**result));
            });
        *subBox = ws.Events().On<DocumentClosed>([docId, handle](const DocumentClosed& ev) mutable {
            if (ev.id == docId) handle.Cancel();
        });
        return ViewerKind::Text;
    }
    case ViewerKind::None:
    default:
        return ViewerKind::None; // unreachable: handled by the early return above
    }
}

} // namespace Onyx::App
