#include <Onyx/App/ViewerOpening.h>

#include <Onyx/Modules/Workspace.h>
#include <Onyx/Services/Logger.h>
#include <Onyx/Types/TypeCatalog.h>

namespace Onyx::App {

using Onyx::Domain::AssetEntry;
using Onyx::Domain::NodeFlags;
using Onyx::Modules::DecodeContext;
using Onyx::Modules::Document;
using Onyx::Modules::SelectionChanged;
using Onyx::Modules::Workspace;
using Onyx::Services::Progress;
using Onyx::Services::Severity;

ViewerKind OpenSelection(Workspace& ws, const SelectionChanged& sel, const ViewerOpener& opener) {
    Document* doc = ws.Get(sel.doc);
    if (!doc) return ViewerKind::None;

    const AssetEntry* entry = Onyx::Modules::Resolve(*doc, sel.path);
    if (!entry) return ViewerKind::None;

    if (entry->flags == NodeFlags::Failed) {
        LOG_WARN("%s: not decoding a Failed node (%zu error diag(s))", entry->name.c_str(),
                 doc->diags.Count(Severity::Error));
        return ViewerKind::None;
    }

    Modules::DecoderRegistry& decoders = ws.Decoders();
    const ViewerKind kind = RouteForType(decoders, entry->typeId);

    Progress progress;
    DecodeContext ctx{*doc, *entry, doc->diags, progress};

    switch (kind) {
    case ViewerKind::Scene: {
        auto scene = decoders.DecodeScene(ctx);
        if (!scene) return ViewerKind::None;
        if (opener.openScene) opener.openScene(entry->name, std::move(scene));
        return ViewerKind::Scene;
    }
    case ViewerKind::Image: {
        auto texture = decoders.DecodeImage(ctx);
        if (!texture) return ViewerKind::None;
        if (opener.openImage) opener.openImage(entry->name, std::move(texture));
        return ViewerKind::Image;
    }
    case ViewerKind::Text: {
        auto text = decoders.DecodeText(ctx);
        if (!text) return ViewerKind::None;
        if (opener.openText) opener.openText(entry->name, std::move(*text));
        return ViewerKind::Text;
    }
    case ViewerKind::None:
    default:
        LOG_INFO("no viewer for %s", std::string(ws.Catalog().KeyOf(entry->typeId)).c_str());
        return ViewerKind::None;
    }
}

} // namespace Onyx::App
