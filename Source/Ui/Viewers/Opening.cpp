#include "Onyx/Ui/Viewers/Opening.h"

#include <Onyx/Core/Modules/DecodeContext.h>
#include <Onyx/Core/Modules/Selection.h>
#include <Onyx/Core/Services/Diagnostics.h>
#include <Onyx/Core/Services/Jobs.h>
#include <Onyx/Core/Vfs/Reader.h>
#include <Onyx/Ui/ViewerRegistry.h>

#include <optional>
#include <string>
#include <utility>

namespace Onyx::Ui {

namespace {

using Domain::MediaKind;

Result<void> Fail(std::string code, std::string message) {
    return Result<void>::Fail(Error{std::move(code), std::move(message), 0});
}

bool NeedsDecode(MediaKind kind) {
    switch (kind) {
    case MediaKind::Image:
    case MediaKind::Mesh:
    case MediaKind::Audio:
    case MediaKind::Text:
    case MediaKind::Table: return true;
    default: return false;
    }
}

// What one open decodes. A worker fills it; the continuation in Pump() reads it.
struct Decoded {
    Domain::AssetEntry entry;
    Services::Settings settings;
    Services::DiagSink diags;
    std::optional<Domain::Texture> image;
    std::optional<Domain::Text> text;
    std::unique_ptr<Domain::Scene> scene;
    std::optional<Domain::Audio> audio;
    std::optional<Domain::Table> table;
    std::shared_ptr<Modules::Document> owner;
    bool ok = false;

    Decoded(const Domain::AssetEntry& e, Services::Settings s) : entry(e), settings(std::move(s)) {}
};

void Decode(Decoded& out, const Modules::DecoderRegistry& decoders, MediaKind kind, Modules::DecodeContext& ctx) {
    switch (kind) {
    case MediaKind::Image:
        out.image = decoders.DecodeImage(ctx);
        out.ok = out.image.has_value();
        break;
    case MediaKind::Text:
        out.text = decoders.DecodeText(ctx);
        out.ok = out.text.has_value();
        break;
    case MediaKind::Mesh:
        out.scene = decoders.DecodeScene(ctx);
        out.ok = out.scene != nullptr;
        break;
    case MediaKind::Audio:
        out.audio = decoders.DecodeAudio(ctx);
        out.ok = out.audio.has_value();
        break;
    case MediaKind::Table:
        out.table = decoders.DecodeTable(ctx);
        out.ok = out.table.has_value();
        break;
    default: break;
    }
}

using Create = std::function<std::unique_ptr<IDocumentView>(OpenRequest&)>;
using Place = std::function<void(std::unique_ptr<IDocumentView>)>;

void Finish(OpenRequest& request, const Create& create, const Place& place) {
    if (!create || !place) return;
    if (std::unique_ptr<IDocumentView> view = create(request)) place(std::move(view));
}

}

Result<void> OpenEntry(UiContext& ui, const Selection& selection, std::function<void(std::unique_ptr<IDocumentView>)> place) {
    Modules::Workspace& workspace = ui.workspace;
    Modules::Document* doc = workspace.Get(selection.doc);
    if (!doc) return Fail("ui.open.entry", "no document with id " + std::to_string(selection.doc));
    if (!doc->ready) return Fail("ui.open.entry", "the document is still being read");
    const Domain::AssetEntry* entry = Modules::Resolve(*doc, selection.node);
    if (!entry) return Fail("ui.open.entry", "the document has no entry at that path");

    const ViewerSpec* spec = ui.viewers.Route(workspace.Catalog(), workspace.Decoders(), entry->type);
    if (!spec) return Fail("ui.viewer.none", "no viewer handles " + entry->name);

    const MediaKind kind = workspace.Catalog().Media(entry->type);
    Create create = spec->create;

    if (!NeedsDecode(kind)) {
        OpenRequest request{workspace, selection.doc, selection.node, entry->type, kind, entry->name,
                            std::nullopt, std::nullopt, nullptr, std::nullopt, std::nullopt, {}};
        Finish(request, create, place);
        return Result<void>::Ok();
    }

    if (entry->source.fileIndex >= doc->files.size()) {
        return Fail("ui.open.range", entry->name + " names file " + std::to_string(entry->source.fileIndex) +
                                         " but the document holds " + std::to_string(doc->files.size()));
    }

    std::shared_ptr<Modules::Document> owner;
    for (const std::shared_ptr<Modules::Document>& candidate : workspace.Documents()) {
        if (candidate.get() == doc) owner = candidate;
    }
    if (!owner) return Fail("ui.open.entry", "the document is not open");

    auto decoded = std::make_shared<Decoded>(*entry, workspace.Settings().Clone());
    Modules::Workspace* const ws = &workspace;
    const Selection where = selection;

    // The strong reference to the document travels inside `decoded` and is released in Pump(), never
    // on the worker: a close that lands before the continuation must not run ~Document there.
    std::weak_ptr<Modules::Document> weak = owner;
    owner.reset();

    workspace.Jobs().Submit(
        kDecodeLane,
        [decoded, weak, ws, kind](Services::Progress& progress) {
            decoded->owner = weak.lock();
            Modules::Document* doc = decoded->owner.get();
            if (!doc) return;
            const Domain::ByteRange& range = decoded->entry.source;
            Modules::DecodeContext ctx{doc,
                                       decoded->entry,
                                       Vfs::Reader(doc->files[range.fileIndex], range.offset, range.size),
                                       decoded->diags,
                                       progress,
                                       decoded->settings,
                                       doc->moduleState};
            Decode(*decoded, ws->Decoders(), kind, ctx);
        },
        [decoded, ws, where, kind, create, place = std::move(place)]() {
            const std::shared_ptr<Modules::Document> owner = std::move(decoded->owner);
            if (!owner) return;
            for (Services::Diag& d : decoded->diags.Drain()) owner->diags.Report(std::move(d));
            if (ws->Get(where.doc) != owner.get()) return;
            if (!decoded->ok) {
                owner->diags.Report({Services::Severity::Error, "ui.open.decode",
                                     decoded->entry.name + " could not be decoded for viewing", std::nullopt});
                return;
            }
            OpenRequest request{*ws, where.doc, where.node, decoded->entry.type, kind, decoded->entry.name,
                                std::move(decoded->image), std::move(decoded->text), std::move(decoded->scene),
                                std::move(decoded->audio), std::move(decoded->table), {}};
            Finish(request, create, place);
        });
    return Result<void>::Ok();
}

}
