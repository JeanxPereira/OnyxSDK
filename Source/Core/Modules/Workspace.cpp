#include <Onyx/Core/Modules/Workspace.h>

#include <Onyx/Core/Services/Log.h>
#include <Onyx/Core/Vfs/OsFile.h>

#include <algorithm>
#include <cctype>
#include <exception>
#include <unordered_set>

namespace Onyx::Modules {

namespace {

constexpr const char* kLogCategory = "workspace";

std::string Utf8(const std::filesystem::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::string LowerExtension(const std::filesystem::path& p) {
    std::string ext = Utf8(p.extension());
    if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

void Report(Services::DiagSink& diags, Services::Severity severity, std::string code, std::string message) {
    diags.Report(Services::Diag{severity, std::move(code), std::move(message), std::nullopt});
}

}

Workspace::Workspace(WorkspaceOptions options)
    : m_settings(Services::Settings::Load(options.settingsPath))
    , m_jobs(options.workerThreads) {}

Workspace::~Workspace() = default;

Result<void> Workspace::AddModule(std::unique_ptr<IGameModule> module) {
    if (!module) return Result<void>::Fail({"workspace.nullModule", "module is null", 0});
    const std::string id = module->Info().id;
    if (std::any_of(m_slots.begin(), m_slots.end(), [&](const Slot& s) { return s.module->Info().id == id; })) {
        return Result<void>::Fail({"workspace.duplicateModule", "module id '" + id + "' is already registered", 0});
    }

    const std::vector<Types::TypeId> before = m_catalog.All();
    Types::TypeRegistrar registrar(m_catalog, id);
    module->RegisterTypes(registrar);
    module->RegisterDecoders(m_decoders);

    Slot slot;
    const std::unordered_set<Types::TypeId> known(before.begin(), before.end());
    for (Types::TypeId t : m_catalog.All()) {
        if (!known.contains(t)) slot.types.push_back(t);
    }

    Services::SettingsSchema& schema = m_schemas[id];
    module->RegisterSettings(schema);
    {
        std::lock_guard lock(m_settingsMutex);
        Services::ApplyDefaults(schema, m_settings);
    }

    slot.module = std::move(module);
    m_slots.push_back(std::move(slot));
    RebuildView();
    return Result<void>::Ok();
}

bool Workspace::RemoveModule(std::string_view id) {
    auto it = std::find_if(m_slots.begin(), m_slots.end(), [&](const Slot& s) { return s.module->Info().id == id; });
    if (it == m_slots.end()) return false;

    const IGameModule* module = it->module.get();
    std::vector<DocumentId> owned;
    for (const auto& d : m_documents) {
        if (d->module == module) owned.push_back(d->id);
    }
    for (DocumentId doc : owned) Close(doc);

    m_decoders.RemoveAll(it->types);
    m_schemas.erase(m_schemas.find(id));
    m_slots.erase(it);
    RebuildView();
    return true;
}

void Workspace::RebuildView() {
    m_view.clear();
    for (const Slot& s : m_slots) m_view.push_back(s.module.get());
}

std::span<IGameModule* const> Workspace::Modules() const {
    return m_view;
}

IGameModule* Workspace::FindModule(std::string_view idOrHint) const {
    for (IGameModule* m : m_view) {
        if (m->Info().id == idOrHint) return m;
    }
    for (IGameModule* m : m_view) {
        for (const std::string& hint : m->Info().hints) {
            if (hint == idOrHint) return m;
        }
    }
    return nullptr;
}

std::shared_ptr<IGameModule> Workspace::Owner(const IGameModule* module) const {
    for (const Slot& s : m_slots) {
        if (s.module.get() == module) return s.module;
    }
    return nullptr;
}

ProbeRanking Workspace::Probe(const std::filesystem::path& path) const {
    return RankProbes(m_view, path);
}

Services::SettingsSchema& Workspace::ModuleSettings(std::string_view moduleId) {
    auto it = m_schemas.find(moduleId);
    if (it == m_schemas.end()) it = m_schemas.emplace(std::string(moduleId), Services::SettingsSchema{}).first;
    return it->second;
}

Result<void> Workspace::SetWorkspaceSettingsPath(const std::filesystem::path& path) {
    std::lock_guard lock(m_settingsMutex);
    if (m_opened) {
        return Result<void>::Fail({"workspace.settingsLocked", "settings path must be set before the first open", 0});
    }
    m_settings = Services::Settings::Load(path);
    for (const auto& [id, schema] : m_schemas) Services::ApplyDefaults(schema, m_settings);
    return Result<void>::Ok();
}

std::shared_ptr<Document> Workspace::Prepare(const std::filesystem::path& path, std::string_view moduleHint,
                                             std::shared_ptr<IGameModule>& outModule) {
    IGameModule* module = nullptr;
    if (!moduleHint.empty()) {
        module = FindModule(moduleHint);
        if (!module) {
            ONYX_LOG_WARN(kLogCategory, "no module matches hint '{}'", std::string(moduleHint));
            return nullptr;
        }
    } else {
        const ProbeRanking ranking = Probe(path);
        module = ranking.winner;
        if (!module) {
            ONYX_LOG_WARN(kLogCategory, "no module accepted '{}'", Utf8(path));
            return nullptr;
        }
    }
    outModule = Owner(module);

    {
        std::lock_guard lock(m_settingsMutex);
        m_opened = true;
    }

    auto doc = std::make_shared<Document>();
    doc->id = m_nextId++;
    doc->path = path;
    doc->module = module;
    if (auto file = Vfs::OsFile::Open(path); file.ok()) doc->files.push_back(std::move(file.value()));

    m_documents.push_back(doc);
    m_events.Post(DocumentOpened{doc->id});
    return doc;
}

bool Workspace::Parse(Document& doc, IGameModule& module, Services::Progress& progress) {
    if (doc.files.empty()) {
        Report(doc.diags, Services::Severity::Error, "workspace.open.failed", "failed to open file: " + Utf8(doc.path));
        doc.ready = true;
        return false;
    }

    const std::string ext = LowerExtension(doc.path);
    try {
        for (const MountSpec& spec : module.Mounts()) {
            if (std::find(spec.extensions.begin(), spec.extensions.end(), ext) == spec.extensions.end()) continue;
            if (auto vfs = spec.mount(doc.path)) {
                doc.mounted = std::move(vfs);
            } else {
                Report(doc.diags, Services::Severity::Warning, module.Info().id + ".mount-refused",
                       "mount refused, parsing as flat file");
            }
            break;
        }
    } catch (const std::exception& e) {
        doc.mounted.reset();
        Report(doc.diags, Services::Severity::Error, module.Info().id + ".mount-threw",
               std::string("Mounts()/mount threw: ") + e.what());
    } catch (...) {
        doc.mounted.reset();
        Report(doc.diags, Services::Severity::Error, module.Info().id + ".mount-threw", "Mounts()/mount threw");
    }

    ContainerContext ctx{Vfs::Reader(doc.files.front()), doc.path, m_settings, doc.diags, progress,
                         doc.moduleState,                doc.roots, doc.mounted.get(), doc.files};
    ParseResult result;
    try {
        result = module.ParseContainer(ctx);
    } catch (...) {
        Report(doc.diags, Services::Severity::Error, module.Info().id + ".parse-threw", "ParseContainer threw");
        result = ParseResult{false};
    }
    doc.ready = true;
    return result.ok;
}

DocumentId Workspace::Open(const std::filesystem::path& path, std::string_view moduleHint) {
    std::shared_ptr<IGameModule> module;
    std::shared_ptr<Document> doc = Prepare(path, moduleHint, module);
    if (!doc) return 0;

    Services::Progress progress;
    const bool ok = Parse(*doc, *module, progress);
    m_events.Post(TreeReady{doc->id, ok});
    return doc->id;
}

DocumentId Workspace::OpenAsync(const std::filesystem::path& path, std::string_view moduleHint) {
    std::shared_ptr<IGameModule> module;
    std::shared_ptr<Document> doc = Prepare(path, moduleHint, module);
    if (!doc) return 0;

    const DocumentId id = doc->id;
    auto ok = std::make_shared<std::atomic<bool>>(false);
    doc->parseJob = m_jobs.Submit(
        id,
        [this, doc, module, ok](Services::Progress& progress) { ok->store(Parse(*doc, *module, progress)); },
        [this, id, ok]() {
            if (Get(id)) m_events.Post(TreeReady{id, ok->load()});
        });
    return id;
}

Document* Workspace::Get(DocumentId id) {
    for (const auto& d : m_documents) {
        if (d->id == id) return d.get();
    }
    return nullptr;
}

std::span<const std::shared_ptr<Document>> Workspace::Documents() const {
    return m_documents;
}

void Workspace::Close(DocumentId id) {
    auto it = std::find_if(m_documents.begin(), m_documents.end(), [id](const auto& d) { return d->id == id; });
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

void Workspace::Pump() {
    m_jobs.Pump();
    m_events.Pump();
}

}
