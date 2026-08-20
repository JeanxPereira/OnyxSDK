#include <Onyx/Modules/Workspace.h>
#include <Onyx/Modules/DecoderRegistry.h>
#include <Onyx/Services/Logger.h>
#include <Onyx/Vfs/OsFile.h>

#include <algorithm>
#include <cctype>
#include <exception>

namespace Onyx::Modules {

Workspace::Workspace(Types::TypeCatalog& catalog)
    : m_catalog(catalog)
    , m_modules()
    , m_decoders(std::make_unique<DecoderRegistry>())
    , m_settings(Services::Settings::Load(std::filesystem::path{}))
    , m_events()
    , m_documents()
    , m_nextId(1)
    , m_jobs(2) {
}

Workspace::~Workspace() = default;

void Workspace::AddModule(std::unique_ptr<IGameModule> m) {
    if (!m) return;
    const std::string id = m->Info().id;
    for (const auto& existing : m_modules) {
        if (existing->Info().id == id) {
            // A duplicate id would collide in FindModule (by id) and in
            // the TypeCatalog's per-module namespace -- refuse it outright
            // rather than silently shadowing the first registration.
            ONYX_LOGF_ERR("[Workspace] module id '%s' already registered; ignoring duplicate",
                    id.c_str());
            return;
        }
    }
    Types::TypeRegistrar registrar(m_catalog, id);
    m->RegisterTypes(registrar);
    m->RegisterDecoders(*m_decoders);
    m_modules.push_back(std::move(m));
}

const std::vector<std::unique_ptr<IGameModule>>& Workspace::Modules() const {
    return m_modules;
}

IGameModule* Workspace::FindModule(std::string_view idOrHint) const {
    for (auto& m : m_modules) {
        if (m->Info().id == idOrHint) return m.get();
    }
    for (auto& m : m_modules) {
        for (auto& hint : m->Info().hints) {
            if (hint == idOrHint) return m.get();
        }
    }
    return nullptr;
}

ProbeRanking Workspace::Probe(const std::filesystem::path& path) const {
    std::vector<IGameModule*> modules;
    modules.reserve(m_modules.size());
    for (auto& m : m_modules) modules.push_back(m.get());
    return RankProbes(modules, path);
}

DocumentId Workspace::AllocateId() {
    return m_nextId++;
}

std::shared_ptr<Document> Workspace::PrepareDocument(const std::filesystem::path& path,
                                                      std::string_view moduleHint,
                                                      IGameModule*& outModule) {
    IGameModule* module = nullptr;
    if (!moduleHint.empty()) {
        // Hint wins outright: no fallback to probing when it fails to
        // resolve, per the spec's "hint wins when given".
        module = FindModule(moduleHint);
    } else if (m_modules.size() == 1) {
        // Spec §5.1: with exactly one registered module, the probe step
        // is skipped entirely -- there is nothing to disambiguate, so the
        // confidence floor (kProbeFloor) never applies here even when
        // that module's own Probe() would score low.
        module = m_modules.front().get();
    } else {
        module = Probe(path).winner;
    }
    if (!module) {
        outModule = nullptr;
        return nullptr;
    }
    outModule = module;

    auto doc = std::make_shared<Document>();
    doc->id = AllocateId();
    doc->path = path;
    doc->module = module;
    doc->file = std::make_shared<Vfs::OsFile>(path.string());
    doc->fileTable.push_back(doc->file);   // slot 0: the root container file
    doc->ready = false;

    m_documents.push_back(doc);
    m_events.Post(DocumentOpened{doc->id});
    return doc;
}

ParseResult Workspace::RunParse(Document& doc, IGameModule& module,
                                 Services::Progress& progress) {
    if (!doc.file || !doc.file->IsValid()) {
        doc.diags.Report(Services::Diag{
            Services::Severity::Error,
            "workspace.open.failed",
            "failed to open file: " + doc.path.string(),
            std::nullopt});
        doc.ready = true;
        return ParseResult{false};
    }

    // Mount processing at open (spec §5.2, Task 7). Runs here -- on the
    // same thread as ParseContainer below (the OpenAsync worker, or the
    // caller's own thread for synchronous Open) -- so it obeys the same
    // thread contract Document documents for roots/diags/state: nothing
    // written here is safe to read via Get() until TreeReady fires.
    // Extension match is case-insensitive against MountSpec::extensions
    // (lowercase, no dot); the first matching spec wins. A factory that
    // returns nullptr is a refused mount, not a parse failure: fall
    // through to a plain-file parse (ctx.mountedVfs/fileTable stay null)
    // with a Warning diag instead of aborting the open.
    std::string ext = doc.path.extension().string();
    if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
    std::transform(ext.begin(), ext.end(), ext.begin(),
                    [](unsigned char c) { return char(std::tolower(c)); });

    // Mounts() and the chosen spec's factory are module-supplied callables,
    // exactly like ParseContainer below -- spec §7.1 forbids letting either
    // throw across the module boundary. Contained the same way: an Error
    // diag, then fall through to a flat-file parse (never abort the open).
    // Without this, a throw here would escape Open() uncontained and, on
    // the OpenAsync path, strand doc.ready == false forever -- only
    // JobQueue's own generic catch would ever see it.
    try {
        for (const MountSpec& spec : module.Mounts()) {
            if (std::find(spec.extensions.begin(), spec.extensions.end(), ext) ==
                spec.extensions.end()) {
                continue;
            }
            if (auto vfs = spec.mount(doc.path)) {
                doc.mountedVfs = std::move(vfs);
            } else {
                doc.diags.Report(Services::Diag{
                    Services::Severity::Warning,
                    module.Info().id + ".mount-refused",
                    "mount refused, parsing as flat file",
                    std::nullopt});
            }
            break;
        }
    } catch (const std::exception& e) {
        doc.mountedVfs.reset();
        doc.diags.Report(Services::Diag{
            Services::Severity::Error,
            module.Info().id + ".mount-threw",
            std::string("Mounts()/mount threw: ") + e.what(),
            std::nullopt});
    } catch (...) {
        doc.mountedVfs.reset();
        doc.diags.Report(Services::Diag{
            Services::Severity::Error,
            module.Info().id + ".mount-threw",
            "Mounts()/mount threw",
            std::nullopt});
    }

    ContainerContext ctx{*doc.file, doc.path, m_settings, doc.diags, progress,
                          doc.state, doc.roots,
                          doc.mountedVfs.get(),
                          doc.mountedVfs ? &doc.fileTable : nullptr};
    ParseResult result;
    try {
        result = module.ParseContainer(ctx);
    } catch (...) {
        // No exceptions cross the module boundary (spec §7.1): a module
        // that throws is treated exactly like one that reports an Error
        // and returns ok=false -- the caller (Open, synchronously; or the
        // JobQueue's own catch, for OpenAsync) must never see this throw.
        doc.diags.Report(Services::Diag{
            Services::Severity::Error,
            module.Info().id + ".parse-threw",
            "ParseContainer threw",
            std::nullopt});
        result = ParseResult{false};
    }
    doc.ready = true;
    return result;
}

DocumentId Workspace::Open(const std::filesystem::path& path, std::string_view moduleHint) {
    IGameModule* module = nullptr;
    std::shared_ptr<Document> doc = PrepareDocument(path, moduleHint, module);
    if (!doc) return 0;

    Services::Progress progress;
    ParseResult result = RunParse(*doc, *module, progress);
    m_events.Post(TreeReady{doc->id, result.ok});
    return doc->id;
}

DocumentId Workspace::OpenAsync(const std::filesystem::path& path, std::string_view moduleHint) {
    IGameModule* module = nullptr;
    std::shared_ptr<Document> doc = PrepareDocument(path, moduleHint, module);
    if (!doc) return 0;

    DocumentId id = doc->id;
    auto ok = std::make_shared<bool>(false);

    doc->parseJob = m_jobs.Submit(
        id,
        // `doc` is captured BY VALUE (shared_ptr): this keeps the
        // Document object alive for the duration of RunParse even if
        // Close(id) erases Workspace's own entry from m_documents while
        // this job is in flight -- Get(id) goes null immediately, but the
        // worker's copy of the shared_ptr keeps the object itself alive
        // until this lambda returns.
        [this, doc, module, ok](Services::Progress& progress) {
            ParseResult result = RunParse(*doc, *module, progress);
            *ok = result.ok;
        },
        [this, id, ok]() {
            // Look the document up BY ID, not by the captured pointer: if
            // Close() ran while the job was in flight, the document is
            // gone and there is nothing to report TreeReady for.
            Document* d = Get(id);
            if (!d) return;
            m_events.Post(TreeReady{id, *ok});
        });

    return id;
}

Document* Workspace::Get(DocumentId id) {
    for (auto& d : m_documents) {
        if (d->id == id) return d.get();
    }
    return nullptr;
}

const std::vector<std::shared_ptr<Document>>& Workspace::Documents() const {
    return m_documents;
}

void Workspace::Close(DocumentId id) {
    auto it = std::find_if(m_documents.begin(), m_documents.end(),
                            [id](const std::shared_ptr<Document>& d) { return d->id == id; });
    if (it == m_documents.end()) return;
    m_documents.erase(it);
    m_events.Post(DocumentClosed{id});
}

bool Workspace::CancelOpen(DocumentId id) {
    Document* doc = Get(id);
    if (!doc || !doc->parseJob.Valid()) return false;
    doc->parseJob.Cancel();
    return true;
}

Services::EventBus& Workspace::Events() { return m_events; }
Services::JobQueue& Workspace::Jobs() { return m_jobs; }
Services::Settings& Workspace::WorkspaceSettings() { return m_settings; }

void Workspace::SetWorkspaceSettingsPath(const std::filesystem::path& path) {
    m_settings = Services::Settings::Load(path);
}

class DecoderRegistry& Workspace::Decoders() { return *m_decoders; }
Types::TypeCatalog& Workspace::Catalog() { return m_catalog; }

} // namespace Onyx::Modules
