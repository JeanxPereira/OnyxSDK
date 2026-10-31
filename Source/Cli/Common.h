#pragma once

#include <Onyx/Cli/StockVerbs.h>
#include <Onyx/Core/Cli/Resolve.h>
#include <Onyx/Core/Domain/AssetEntry.h>
#include <Onyx/Core/Modules/Document.h>
#include <Onyx/Core/Modules/Workspace.h>
#include <Onyx/Core/Vfs/IFile.h>

#include <filesystem>
#include <memory>
#include <iosfwd>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Onyx::Cli::Detail {

struct Args {
    std::vector<std::string_view> positional;
    bool json = false;
    bool strict = false;
    bool first = false;
    std::string_view game;
    std::string_view to;
    std::string_view out;
};

// Flags may appear anywhere after the verb. False for an unknown `--` token or a value flag with
// nothing after it.
bool ParseArgs(std::span<const std::string_view> args, Args& parsed);

// Opens a document for the lifetime of the guard and closes it afterwards.
class OpenDocument {
public:
    OpenDocument(Modules::Workspace& ws, const std::filesystem::path& path, std::string_view moduleHint);
    ~OpenDocument();

    OpenDocument(const OpenDocument&) = delete;
    OpenDocument& operator=(const OpenDocument&) = delete;

    bool ok() const { return m_doc != nullptr; }
    Modules::Document& doc() { return *m_doc; }

private:
    Modules::Workspace& m_ws;
    Modules::DocumentId m_id = 0;
    Modules::Document* m_doc = nullptr;
};

// Names the failure on `out` and returns false when no module accepts the file.
bool OpenOrReport(OpenDocument& opened, const std::filesystem::path& path, std::ostream& out);

bool IsFailed(const Domain::AssetEntry& entry);

// Null when the entry points at a slot the module never registered.
std::shared_ptr<const Vfs::IFile> FileOf(const Modules::Document& doc, const Domain::AssetEntry& entry);

Verb ProbeVerb();
Verb ListVerb();
Verb ExtractVerb();
Verb DecodeVerb();

}
