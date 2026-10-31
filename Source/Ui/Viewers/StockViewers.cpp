#include "Onyx/Ui/Viewers/StockViewers.h"

#include <Onyx/Core/Modules/Selection.h>
#include <Onyx/Ui/Viewers/AudioView.h>
#include <Onyx/Ui/Viewers/HexView.h>
#include <Onyx/Ui/Viewers/ImageView.h>
#include <Onyx/Ui/Viewers/TableView.h>
#include <Onyx/Ui/Viewers/TextView.h>

#include <memory>
#include <utility>

namespace Onyx::Ui {

namespace {

using Domain::MediaKind;

std::unique_ptr<IDocumentView> CreateImage(OpenRequest& r) {
    if (!r.image) return nullptr;
    return std::make_unique<ImageView>(r.doc, std::move(r.node), std::move(r.name), std::move(*r.image));
}

std::unique_ptr<IDocumentView> CreateText(OpenRequest& r) {
    if (!r.text) return nullptr;
    return std::make_unique<TextView>(r.doc, std::move(r.node), std::move(r.name), std::move(*r.text));
}

std::unique_ptr<IDocumentView> CreateTable(OpenRequest& r) {
    if (!r.table) return nullptr;
    return std::make_unique<TableView>(r.doc, std::move(r.node), std::move(r.name), std::move(*r.table));
}

std::unique_ptr<IDocumentView> CreateAudio(OpenRequest& r) {
    if (!r.audio) return nullptr;
    return std::make_unique<AudioView>(r.doc, std::move(r.node), std::move(r.name), std::move(*r.audio));
}

// The hex view keeps a reader over the entry's byte range and reads rows as they scroll into view.
std::unique_ptr<IDocumentView> CreateHex(OpenRequest& r) {
    if (!r.raw.empty()) {
        return std::make_unique<HexView>(r.doc, std::move(r.node), std::move(r.name), std::move(r.raw));
    }
    Modules::Document* doc = r.workspace.Get(r.doc);
    const Domain::AssetEntry* entry = doc ? Modules::Resolve(*doc, r.node) : nullptr;
    if (!entry || entry->source.fileIndex >= doc->files.size()) return nullptr;
    const Domain::ByteRange& range = entry->source;
    return std::make_unique<HexView>(r.doc, std::move(r.node), std::move(r.name),
                                     Vfs::Reader(doc->files[range.fileIndex], range.offset, range.size));
}

ViewerSpec Spec(const char* id, MediaKind kind, int priority,
                std::function<std::unique_ptr<IDocumentView>(OpenRequest&)> create) {
    ViewerSpec spec;
    spec.id = id;
    spec.target = kind;
    spec.priority = priority;
    spec.create = std::move(create);
    return spec;
}

}

void RegisterStockViewers(ViewerRegistry& registry) {
    registry.Register(Spec("onyx.image", MediaKind::Image, 0, CreateImage));
    registry.Register(Spec("onyx.text", MediaKind::Text, 0, CreateText));
    registry.Register(Spec("onyx.hex", MediaKind::Raw, -10, CreateHex));
    registry.Register(Spec("onyx.table", MediaKind::Table, 0, CreateTable));
    registry.Register(Spec("onyx.audio", MediaKind::Audio, 0, CreateAudio));
}

}
