#include <Onyx/Modules/Workspace.h>
#include <Onyx/Vfs/OsFile.h>

#include <algorithm>
#include <exception>

namespace Onyx::Modules {

Workspace::Workspace(Types::TypeCatalog& catalog)
    : m_catalog(catalog)
    , m_modules()
    , m_settings(Services::Settings::Load(std::filesystem::path{}))
    , m_events()
    , m_documents()
    , m_nextId(1)
    , m_jobs(2) {
}

Workspace::~Workspace() = default;

void Workspace::AddModule(std::unique_ptr<IGameModule> m) {
    if (!m) return;
    Types::TypeRegistrar registrar(m_catalog, m->Info().id);
    m->RegisterTypes(registrar);
    // Task 3 wires RegisterDecoders
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

    ContainerContext ctx{*doc.file, m_settings, doc.diags, progress,
                          doc.state, doc.roots};
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

    m_jobs.Submit(
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

void Workspace::Close(DocumentId id) {
    auto it = std::find_if(m_documents.begin(), m_documents.end(),
                            [id](const std::shared_ptr<Document>& d) { return d->id == id; });
    if (it == m_documents.end()) return;
    m_documents.erase(it);
    m_events.Post(DocumentClosed{id});
}

Services::EventBus& Workspace::Events() { return m_events; }
Services::JobQueue& Workspace::Jobs() { return m_jobs; }
Services::Settings& Workspace::WorkspaceSettings() { return m_settings; }
Types::TypeCatalog& Workspace::Catalog() { return m_catalog; }

// Workspace::Decoders() is intentionally NOT defined here: DecoderRegistry
// does not exist yet (Task 3 adds the class, the m_decoders member, and
// this method's body). Nothing in M3a calls it.

} // namespace Onyx::Modules
