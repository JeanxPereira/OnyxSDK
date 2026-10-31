#pragma once

#include <doctest/doctest.h>

#include <Prism/Module.h>

#include <Onyx/Core/Modules/Workspace.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <system_error>
#include <vector>

namespace Onyx::Tests {

struct TempFile {
    std::filesystem::path path;

    TempFile(const std::string& name, const std::vector<std::byte>& bytes)
        : path(std::filesystem::temp_directory_path() / ("onyx_prism_" + name)) {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
};

inline std::unique_ptr<Modules::Workspace> NewWorkspace() {
    auto ws = std::make_unique<Modules::Workspace>();
    REQUIRE(ws->AddModule(std::make_unique<Prism::Module>()).ok());
    return ws;
}

// A decode context over one entry of an open document.
struct Decoding {
    Services::Progress progress;
    Modules::DecodeContext ctx;

    Decoding(Modules::Workspace& ws, Modules::Document& doc, const Domain::AssetEntry& entry)
        : ctx{&doc,
              entry,
              Vfs::Reader(doc.files[entry.source.fileIndex], entry.source.offset, entry.source.size),
              doc.diags,
              progress,
              ws.Settings(),
              doc.moduleState} {}
};

inline const Domain::AssetEntry* Find(const Modules::Document& doc, const std::string& name) {
    for (const Domain::AssetEntry& e : doc.roots) {
        if (e.name == name) return &e;
    }
    return nullptr;
}

inline bool HasDiag(Modules::Document& doc, const std::string& code, Services::Severity severity) {
    for (const Services::Diag& d : doc.diags.Snapshot()) {
        if (d.code == code && d.severity == severity) return true;
    }
    return false;
}

}
