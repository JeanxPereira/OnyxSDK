#include <Onyx/TestKit/DecodeSmoke.h>

#include <Onyx/Domain/Entry.h>
#include <Onyx/Modules/DecoderRegistry.h>
#include <Onyx/Services/Diagnostics.h>
#include <Onyx/Services/Jobs.h>

#include <algorithm>

namespace Onyx::TestKit {

namespace {

bool InAllowlist(const std::vector<std::string>& allowlist, const std::string& name) {
    return std::find(allowlist.begin(), allowlist.end(), name) != allowlist.end();
}

// One entry's decode attempt. Priority mirrors Source/Cli/Commands.cpp's
// CmdDecode exactly (Scene > Image > Text -- spec §11's shared-routing
// contract): only one branch ever runs per entry.
void DecodeOne(Modules::Workspace& ws, Modules::Document& doc, const Domain::AssetEntry& entry,
                const std::vector<std::string>& allowlist, SmokeResult& out) {
    Modules::DecoderRegistry& reg = ws.Decoders();
    Services::Progress progress;

    bool hadCapability = false;
    bool decodedOk = false;

    if (reg.HasScene(entry.typeId)) {
        hadCapability = true;
        Modules::DecodeContext ctx{doc, entry, doc.diags, progress};
        decodedOk = static_cast<bool>(reg.DecodeScene(ctx));
    } else if (reg.HasImage(entry.typeId)) {
        hadCapability = true;
        Modules::DecodeContext ctx{doc, entry, doc.diags, progress};
        decodedOk = static_cast<bool>(reg.DecodeImage(ctx));
    } else if (reg.HasText(entry.typeId)) {
        hadCapability = true;
        Modules::DecodeContext ctx{doc, entry, doc.diags, progress};
        decodedOk = reg.DecodeText(ctx).has_value();
    }

    if (!hadCapability) {
        ++out.skipped;
        return;
    }
    if (decodedOk) {
        ++out.decoded;
        return;
    }
    // A capability existed but the decoder salvage-failed (returned null).
    if (InAllowlist(allowlist, entry.name)) {
        ++out.skipped;
        return;
    }
    ++out.failed;
    out.errors.push_back(entry.name + ": decode returned null");
}

void WalkEntries(Modules::Workspace& ws, Modules::Document& doc,
                  const std::vector<Domain::AssetEntry>& entries,
                  const std::vector<std::string>& allowlist, SmokeResult& out) {
    for (const auto& e : entries) {
        DecodeOne(ws, doc, e, allowlist, out);
        WalkEntries(ws, doc, e.children, allowlist, out);
    }
}

} // namespace

SmokeResult DecodeAll(Modules::Workspace& ws, Modules::DocumentId id,
                       const std::vector<std::string>& allowlist) {
    SmokeResult out;
    Modules::Document* doc = ws.Get(id);
    if (!doc) return out;

    WalkEntries(ws, *doc, doc->roots, allowlist, out);
    return out;
}

} // namespace Onyx::TestKit
