#include "Common.h"

#include <ostream>

namespace Onyx::Cli::Detail {

Args ParseArgs(std::span<const std::string_view> args) {
    Args parsed;
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string_view a = args[i];
        const bool hasValue = i + 1 < args.size();
        if (a == "--json") {
            parsed.json = true;
        } else if (a == "--strict") {
            parsed.strict = true;
        } else if (a == "--first") {
            parsed.first = true;
        } else if (a == "--game") {
            if (hasValue) parsed.game = args[++i];
        } else if (a == "--to") {
            if (hasValue) parsed.to = args[++i];
        } else if (a == "--out") {
            if (hasValue) parsed.out = args[++i];
        } else {
            parsed.positional.push_back(a);
        }
    }
    return parsed;
}

OpenDocument::OpenDocument(Modules::Workspace& ws, const std::filesystem::path& path, std::string_view moduleHint)
    : m_ws(ws) {
    m_id = ws.Open(path, moduleHint);
    if (m_id != 0) m_doc = ws.Get(m_id);
}

OpenDocument::~OpenDocument() {
    if (m_id != 0) m_ws.Close(m_id);
}

bool OpenOrReport(OpenDocument& opened, const std::filesystem::path& path, std::ostream& out) {
    if (opened.ok()) return true;
    out << "no module accepts " << path.string() << "\n";
    return false;
}

bool IsFailed(const Domain::AssetEntry& entry) {
    return Domain::HasFlag(entry.flags, Domain::NodeFlags::Failed);
}

std::shared_ptr<const Vfs::IFile> FileOf(const Modules::Document& doc, const Domain::AssetEntry& entry) {
    if (entry.source.fileIndex >= doc.files.size()) return nullptr;
    return doc.files[entry.source.fileIndex];
}

}

namespace Onyx::Cli {

std::vector<Verb> StockVerbs() {
    return StockVerbs(nullptr);
}

std::vector<Verb> StockVerbs(const Exporters* exporters) {
    return {Detail::ProbeVerb(), Detail::ListVerb(), Detail::ExtractVerb(), Detail::DecodeVerb(exporters)};
}

}
