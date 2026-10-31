#pragma once

#include <doctest/doctest.h>

#include <Prism/Module.h>

#include <Onyx/Core/Modules/Workspace.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
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

// One hand-laid section for files the Writer refuses to produce.
struct RawSection {
    uint8_t kind = 0;
    std::string name;
    std::vector<std::byte> payload;
};

inline void PutLe(std::vector<std::byte>& out, uint64_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) out.push_back(static_cast<std::byte>((value >> (8 * i)) & 0xFF));
}

// A container whose payloads sit back to back after the section headers, whatever their content.
inline std::vector<std::byte> RawPrism(uint16_t paletteCount, const std::vector<RawSection>& sections) {
    std::vector<std::byte> out;
    for (char c : std::string_view("PRM1")) out.push_back(static_cast<std::byte>(c));
    PutLe(out, paletteCount, 2);
    PutLe(out, sections.size(), 4);
    uint64_t cursor = out.size();
    for (const RawSection& s : sections) cursor += 1 + 2 + s.name.size() + 4 + 4;
    for (const RawSection& s : sections) {
        PutLe(out, s.kind, 1);
        PutLe(out, s.name.size(), 2);
        for (char c : s.name) out.push_back(static_cast<std::byte>(c));
        PutLe(out, cursor, 4);
        PutLe(out, s.payload.size(), 4);
        cursor += s.payload.size();
    }
    for (const RawSection& s : sections) out.insert(out.end(), s.payload.begin(), s.payload.end());
    return out;
}

inline std::vector<std::byte> Bytes(std::initializer_list<uint8_t> values) {
    std::vector<std::byte> out;
    for (uint8_t v : values) out.push_back(static_cast<std::byte>(v));
    return out;
}

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
