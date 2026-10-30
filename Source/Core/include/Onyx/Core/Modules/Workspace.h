#pragma once

#include <Onyx/Core/Modules/DecoderRegistry.h>
#include <Onyx/Core/Modules/Document.h>
#include <Onyx/Core/Modules/GameModule.h>
#include <Onyx/Core/Modules/Probe.h>
#include <Onyx/Core/Result.h>
#include <Onyx/Core/Services/EventBus.h>
#include <Onyx/Core/Services/Jobs.h>
#include <Onyx/Core/Services/Settings.h>
#include <Onyx/Core/Services/SettingsSchema.h>
#include <Onyx/Core/Types/TypeCatalog.h>

#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Onyx::Modules {

struct DocumentOpened { DocumentId id; };
struct TreeReady { DocumentId id; bool ok; };
struct DocumentClosed { DocumentId id; };

struct WorkspaceOptions {
    unsigned workerThreads = 2;
    std::filesystem::path settingsPath;
};

// Owns the type catalog, decoder registry, settings, event bus, job queue, modules and documents.
// Everything except the job workers is confined to the owner thread; workers touch only the
// document they parse.
class Workspace {
public:
    explicit Workspace(WorkspaceOptions options = {});
    ~Workspace();

    Workspace(const Workspace&) = delete;
    Workspace& operator=(const Workspace&) = delete;

    // Modules are registered before the first Open or OpenAsync; afterwards this fails with
    // "workspace.locked". Also fails with "workspace.duplicateModule" when the id is registered.
    Result<void> AddModule(std::unique_ptr<IGameModule> module);

    // Closes the module's documents and unregisters its decoders. False for an unknown id.
    bool RemoveModule(std::string_view id);

    std::span<IGameModule* const> Modules() const;
    IGameModule* FindModule(std::string_view idOrHint) const;

    ProbeRanking Probe(const std::filesystem::path& path) const;

    // Always probes unless `moduleHint` names a module (id, then hint). Returns 0, and logs, when
    // no module wins or the hint resolves to nothing.
    DocumentId Open(const std::filesystem::path& path, std::string_view moduleHint = {});

    // Same pipeline on the job queue with lane == document id; TreeReady follows through Pump().
    DocumentId OpenAsync(const std::filesystem::path& path, std::string_view moduleHint = {});

    Document* Get(DocumentId id);
    // Requests cancellation of an in-flight parse; the worker keeps the document alive until done.
    void Close(DocumentId id);

    // Flags the parse job for cooperative cancellation. False when the document is unknown or was
    // not opened asynchronously.
    bool CancelOpen(DocumentId id);

    std::span<const std::shared_ptr<Document>> Documents() const;

    Services::EventBus& Events() { return m_events; }
    Services::JobQueue& Jobs() { return m_jobs; }
    Services::Settings& Settings() { return m_settings; }
    Types::TypeCatalog& Catalog() { return m_catalog; }
    DecoderRegistry& Decoders() { return m_decoders; }
    Services::SettingsSchema& ModuleSettings(std::string_view moduleId);

    // Contract: call before the first Open or OpenAsync, because parses read Settings() through a
    // reference. Fails with "workspace.settingsLocked" once any open has been issued, leaving the
    // current settings untouched.
    Result<void> SetWorkspaceSettingsPath(const std::filesystem::path& path);

    void Pump();

private:
    struct Slot {
        std::shared_ptr<IGameModule> module;
        std::vector<Types::TypeId> types;
    };

    std::shared_ptr<Document> Prepare(const std::filesystem::path& path, std::string_view moduleHint,
                                      std::shared_ptr<IGameModule>& outModule);
    bool Parse(Document& doc, IGameModule& module, Services::Progress& progress);
    std::shared_ptr<IGameModule> Owner(const IGameModule* module) const;
    void RebuildView();

    Types::TypeCatalog m_catalog;
    DecoderRegistry m_decoders;
    Services::Settings m_settings;
    Services::EventBus m_events;
    std::vector<Slot> m_slots;
    std::vector<IGameModule*> m_view;
    std::map<std::string, Services::SettingsSchema, std::less<>> m_schemas;
    std::vector<std::shared_ptr<Document>> m_documents;
    DocumentId m_nextId = 1;
    std::mutex m_settingsMutex;
    bool m_opened = false;
    Services::JobQueue m_jobs;
};

}
