#include <Onyx/TestKit/DecodeSmoke.h>

#include <Onyx/Core/Schema/SchemaNode.h>
#include <Onyx/Core/Services/Jobs.h>

#include <algorithm>

namespace Onyx::TestKit {

namespace {

struct Walker {
    Modules::Workspace& ws;
    Modules::Document& doc;
    const std::vector<std::string>& allowlist;
    SmokeResult& result;

    bool Allowed(const std::string& name) const {
        return std::find(allowlist.begin(), allowlist.end(), name) != allowlist.end();
    }

    Vfs::Reader ReaderFor(const Domain::AssetEntry& entry) const {
        std::shared_ptr<const Vfs::IFile> file;
        if (entry.source.fileIndex < doc.files.size()) file = doc.files[entry.source.fileIndex];
        return Vfs::Reader(std::move(file), entry.source.offset, entry.source.size);
    }

    // Names every reported slot that returned nothing.
    std::vector<std::string> Exercise(const Domain::AssetEntry& entry, const Modules::Capabilities& caps) {
        const Modules::DecoderRegistry& decoders = ws.Decoders();
        Services::Progress progress;
        const auto context = [&] {
            return Modules::DecodeContext{&doc, entry, ReaderFor(entry), doc.diags, progress, ws.Settings(),
                                          doc.moduleState};
        };

        std::vector<std::string> failed;
        if (caps.scene) {
            auto ctx = context();
            if (!decoders.DecodeScene(ctx)) failed.emplace_back("scene");
        }
        if (caps.image) {
            auto ctx = context();
            if (!decoders.DecodeImage(ctx)) failed.emplace_back("image");
        }
        if (caps.text) {
            auto ctx = context();
            if (!decoders.DecodeText(ctx)) failed.emplace_back("text");
        }
        if (caps.audio) {
            auto ctx = context();
            if (!decoders.DecodeAudio(ctx)) failed.emplace_back("audio");
        }
        if (caps.table) {
            auto ctx = context();
            if (!decoders.DecodeTable(ctx)) failed.emplace_back("table");
        }
        if (caps.schema) {
            Vfs::Reader reader = ReaderFor(entry);
            const auto schema = decoders.SchemaOf(entry.type);
            if (!schema || !Schema::ReadSchema(*schema, reader, 0).ok()) failed.emplace_back("schema");
        }
        for (const std::string& capability : caps.any) {
            auto ctx = context();
            if (!decoders.DecodeAny(ctx, capability)) failed.push_back(capability);
        }
        return failed;
    }

    void Visit(const Domain::AssetEntry& entry) {
        if (Domain::HasFlag(entry.flags, Domain::NodeFlags::Failed)) {
            ++result.skippedFailed;
        } else if (const Modules::Capabilities caps = ws.Decoders().Of(entry.type); caps.Empty()) {
            ++result.skipped;
        } else if (const std::vector<std::string> failed = Exercise(entry, caps); failed.empty()) {
            ++result.decoded;
        } else if (Allowed(entry.name)) {
            ++result.skipped;
        } else {
            ++result.failed;
            std::string line = entry.name + ": decode failed (";
            for (size_t i = 0; i < failed.size(); ++i) line += (i ? ", " : "") + failed[i];
            result.errors.push_back(line + ")");
        }

        for (const Domain::AssetEntry& child : entry.children) Visit(child);
    }
};

}

SmokeResult DecodeAll(Modules::Workspace& ws, Modules::DocumentId id, const std::vector<std::string>& allowlist) {
    SmokeResult result;
    Modules::Document* doc = ws.Get(id);
    if (!doc) return result;

    Walker walker{ws, *doc, allowlist, result};
    for (const Domain::AssetEntry& root : doc->roots) walker.Visit(root);
    return result;
}

}
