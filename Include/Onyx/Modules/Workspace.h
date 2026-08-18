#pragma once
#include <Onyx/Modules/GameModule.h>
#include <Onyx/Modules/Probe.h>
#include <Onyx/Services/Diagnostics.h>
#include <Onyx/Services/EventBus.h>
#include <Onyx/Services/Jobs.h>
#include <Onyx/Services/Settings.h>
#include <Onyx/Types/TypeCatalog.h>
#include <Onyx/Domain/Entry.h>
#include <Onyx/Vfs/IFile.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string_view>
#include <vector>

namespace Onyx::Modules {

using DocumentId = uint64_t;   // 0 = invalid

// Per-document opaque module storage (spec §5.3). The module downcasts.
using ModuleState = std::shared_ptr<void>;

struct ContainerContext {
    Vfs::IFile&               file;
    const Services::Settings& settings;   // workspace-scope settings; read-only
                                           // during parse (a thread-safe
                                           // settings view is scheduled with
                                           // the Shell wiring, M3b)
    Services::DiagSink&       diags;
    Services::Progress&       progress;
    ModuleState&              state;      // module writes what it wants kept
    std::vector<Domain::AssetEntry>& roots;   // module fills the tree here
};

struct Document {
    DocumentId  id = 0;
    std::filesystem::path path;
    IGameModule* module = nullptr;        // owned by the Workspace
    std::vector<Domain::AssetEntry> roots;
    std::shared_ptr<Vfs::IFile> file;
    Services::DiagSink diags;
    ModuleState state;
    bool ready = false;                   // true once parse finished (ok or not)
};

// Events (id payloads only, spec §7.4):
struct DocumentOpened { DocumentId id; };
struct TreeReady      { DocumentId id; bool ok; };
struct DocumentClosed { DocumentId id; };

class Workspace {
public:
    // The catalog is handed in (spec §7.5 — no singleton reach). The
    // composition root passes TypeCatalog::Get() until Shell rework.
    explicit Workspace(Types::TypeCatalog& catalog);
    ~Workspace();                         // joins jobs (JobQueue teardown)

    Workspace(const Workspace&) = delete;
    Workspace& operator=(const Workspace&) = delete;

    // Registers the module's types (namespaced by Info().id) and decoders.
    void AddModule(std::unique_ptr<IGameModule> m);
    const std::vector<std::unique_ptr<IGameModule>>& Modules() const;
    IGameModule* FindModule(std::string_view idOrHint) const;   // id, then hints

    ProbeRanking Probe(const std::filesystem::path&) const;

    // Synchronous open: probe (or forced module), parse on the caller's
    // thread. Returns 0 and reports into the document's DiagSink on failure
    // stages that still produce a document; returns 0 with no document only
    // when no module accepts the file.
    DocumentId Open(const std::filesystem::path&, std::string_view moduleHint = {});

    // Async: same pipeline on the JobQueue (lane = document id). TreeReady
    // is posted on the bus; callers Pump() the queue and the bus.
    DocumentId OpenAsync(const std::filesystem::path&, std::string_view moduleHint = {});

    Document*   Get(DocumentId);
    void        Close(DocumentId);        // posts DocumentClosed

    Services::EventBus& Events();
    Services::JobQueue& Jobs();
    Services::Settings& WorkspaceSettings();
    class DecoderRegistry& Decoders();    // defined in Task 3
    Types::TypeCatalog&  Catalog();

private:
    // Shared by Open/OpenAsync. Resolves the module (an explicit hint wins
    // outright over probing — no fallback to probe when a hint is given
    // but unresolved), opens the container file, and creates + registers
    // a Document (posting DocumentOpened). Returns nullptr, with no
    // document created and no event posted, only when no module accepted
    // the file. `outModule` receives the resolved module whenever a
    // Document is returned.
    //
    // Returns shared_ptr<Document>, not a raw pointer: OpenAsync's Work
    // lambda captures this shared_ptr by value, so a Close() racing an
    // in-flight parse removes the document from m_documents (Get()
    // returns null immediately) without freeing the object out from
    // under the worker thread still running RunParse against it.
    std::shared_ptr<Document> PrepareDocument(const std::filesystem::path& path,
                                               std::string_view moduleHint,
                                               IGameModule*& outModule);

    // The parse pipeline shared by Open (called inline, on the caller's
    // thread) and OpenAsync (called from a JobQueue worker, lane ==
    // document id). Runs `module`'s ParseContainer against `doc` (unless
    // the container file never opened, in which case it reports a diag
    // instead), sets doc.ready = true either way, and returns the
    // ParseResult so the caller can post TreeReady. Never posts events
    // itself — Open and OpenAsync differ in when TreeReady is safe to
    // post (OpenAsync must first confirm, by id, that the document was
    // not Closed while the job was in flight).
    ParseResult RunParse(Document& doc, IGameModule& module,
                          Services::Progress& progress);

    DocumentId AllocateId();

    // ── Member order is the destruction-order contract ───────────────────
    // Destruction runs in reverse declaration order. m_jobs is declared
    // LAST so it is destroyed FIRST: ~JobQueue() joins every worker
    // thread, and a worker may still be mid-ParseContainer touching a
    // Document (via the shared_ptr<Document> captured by OpenAsync's Work
    // lambda -- see PrepareDocument/OpenAsync) and posting through
    // m_events -- so both m_documents and m_events (and m_settings,
    // m_modules) must still be alive while that join happens, which
    // reverse-declaration-order destruction guarantees since they are all
    // declared before m_jobs. Only once every worker has been joined does
    // m_documents get torn down, then m_events, then
    // m_settings/m_modules/m_catalog. (m_documents holds shared_ptr, not
    // unique_ptr, precisely so that erasing an entry from Close() while a
    // worker's copy of that shared_ptr is still outstanding does not free
    // the Document out from under the worker -- the worker's copy keeps
    // it alive until RunParse returns and the Work lambda's captures are
    // released.)
    Types::TypeCatalog& m_catalog;
    std::vector<std::unique_ptr<IGameModule>> m_modules;
    Services::Settings m_settings;
    Services::EventBus m_events;
    std::vector<std::shared_ptr<Document>> m_documents;
    DocumentId m_nextId = 1;
    Services::JobQueue m_jobs;             // declared LAST -> destroyed FIRST
};

} // namespace Onyx::Modules
