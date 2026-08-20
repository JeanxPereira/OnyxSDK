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

#include <atomic>
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
    // The container being parsed. Populated from the same value that
    // becomes Document::path -- the path Workspace::Open/OpenAsync was
    // asked to open. For a MOUNTED container (mountedVfs != nullptr below),
    // this still names the OUTER container -- the file the user actually
    // opened -- never an inner file the module reaches through mountedVfs;
    // inner files have no path of their own (see the IFile discussion in
    // the module-contract tests). A module fills
    // Domain::AssetEntry::wadName from this, and any path-relative work at
    // parse time (e.g. checking for a sibling file next to the container)
    // reads it too.
    const std::filesystem::path& path;
    const Services::Settings& settings;   // workspace-scope settings; read-only
                                           // during parse (a thread-safe
                                           // settings view is scheduled with
                                           // the Shell wiring, M3b)
    Services::DiagSink&       diags;
    Services::Progress&       progress;
    ModuleState&              state;      // module writes what it wants kept
    std::vector<Domain::AssetEntry>& roots;   // module fills the tree here

    // Mount processing at open (spec §5.2, Task 7). mountedVfs is null/
    // absent for a plain file (no MountSpec matched the opened path's
    // extension, or the matching spec's factory refused the mount); when a
    // mount succeeded, mountedVfs is the live VFS to open inner files
    // through. mountedVfs is the only honest "was this mounted?" signal --
    // check it, not fileTable, to tell the two cases apart.
    //
    // fileTable, by contrast, is never null: it is always the Document's
    // file table, slot 0 always pre-seeded with the root container file
    // (by the Workspace, before ParseContainer runs) whether or not a
    // mount happened. A module is free to push ANY file its entries need
    // to address into it -- not only inner files opened via mountedVfs,
    // but a decompressed buffer, a synthesised view, or anything else a
    // plain-file parse builds for itself -- and must stamp the resulting
    // index into those entries' AssetEntry::source.fileIndex; a module
    // that fails to do so leaves affected entries pointing at slot 0 (the
    // still-compressed/still-raw container), which is a correctness bug
    // downstream consumers (e.g. CmdExtract) have no way to detect. Index
    // 0 always names the root container file.
    Vfs::IVirtualFileSystem*  mountedVfs = nullptr;
    std::vector<std::shared_ptr<Vfs::IFile>>* fileTable = nullptr;
};

// Thread contract: a Document obtained from Get() must not be read before
// its TreeReady event (async open) -- worker threads write roots/diags/
// state until then. `ready` is the only field safe to poll.
struct Document {
    DocumentId  id = 0;
    std::filesystem::path path;
    IGameModule* module = nullptr;        // owned by the Workspace
    std::vector<Domain::AssetEntry> roots;
    std::shared_ptr<Vfs::IFile> file;

    // Mount processing at open (Task 7). mountedVfs keeps the module's
    // mounted VFS alive for the Document's whole lifetime (inner files it
    // hands out must outlive the parse). fileTable is the file table
    // AssetEntry::source.fileIndex indexes into: slot 0 is pre-seeded with
    // `file` itself (the root container) by the Workspace before
    // ParseContainer runs, whether or not a mount happens -- a module
    // parsing through a mount appends every inner file it opens via
    // mountedVfs, in order, from slot 1 on; a module with no mount at all
    // may append too (e.g. a decompressed buffer its entries address),
    // which is exactly why ContainerContext::fileTable is handed to every
    // module unconditionally, not only mounted ones.
    std::shared_ptr<Vfs::IVirtualFileSystem> mountedVfs;
    std::vector<std::shared_ptr<Vfs::IFile>> fileTable;

    Services::DiagSink diags;
    ModuleState state;
    std::atomic<bool> ready{false};       // true once parse finished (ok or not)
    // parseJob: handle to the in-flight async parse. Only OpenAsync
    // populates it -- a document from the synchronous Open() leaves it
    // default-constructed (Valid() == false, since the parse is already
    // over by the time Open() returns). Callers poll progress via
    // parseJob.Peek() and request cooperative cancellation via
    // Workspace::CancelOpen(id) (which calls parseJob.Cancel() -- see
    // Jobs.h for the cooperative-cancel contract).
    Services::JobHandle parseJob;
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
    // A module whose Info().id duplicates an already-registered module is
    // rejected: logged as an error and dropped without registering (the
    // first registration for that id wins, and Modules().size() does not
    // grow). This guards FindModule's by-id lookup and the TypeCatalog's
    // per-module namespace from silently colliding.
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

    // Every currently-open document, insertion order. Read-only view for
    // callers that need to iterate the whole set (e.g. the Shell's generic
    // document browser, M3b Task 3) rather than look one up by id.
    const std::vector<std::shared_ptr<Document>>& Documents() const;

    // Cooperative cancel of a document's in-flight async parse: calls
    // parseJob.Cancel() (a fire-and-forget flag; the module's
    // ParseContainer must poll it via Progress::CancelRequested() and
    // return on its own -- see Jobs.h). Returns whether a cancel was
    // issued: false if `id` names no document, or if that document's
    // parseJob is not Valid() (e.g. it was opened synchronously via
    // Open(), which never populates parseJob). A cancelled parse still
    // runs the normal OpenAsync completion path -- TreeReady{ok=false}
    // follows once the module notices and returns.
    bool CancelOpen(DocumentId id);

    Services::EventBus& Events();
    Services::JobQueue& Jobs();
    Services::Settings& WorkspaceSettings();

    // Replaces the workspace-scope Settings by loading `path` (via
    // Settings::Load -- a missing file yields an empty, clean instance).
    // Discards whatever was loaded before, including any unsaved
    // Dirty() changes. Must be called before any Open/OpenAsync that
    // relies on workspace settings: ContainerContext::settings is a
    // reference into m_settings, so an in-flight parse (OpenAsync,
    // running on a worker thread) reading through that reference while
    // this swaps the instance out from under it is a data race --
    // Settings is documented not thread-safe.
    void SetWorkspaceSettingsPath(const std::filesystem::path& path);

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
    std::unique_ptr<class DecoderRegistry> m_decoders;
    Services::Settings m_settings;
    Services::EventBus m_events;
    std::vector<std::shared_ptr<Document>> m_documents;
    DocumentId m_nextId = 1;
    Services::JobQueue m_jobs;             // declared LAST -> destroyed FIRST
};

} // namespace Onyx::Modules
