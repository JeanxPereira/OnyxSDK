#include <Onyx/Api/ToolkitApi.h>
#include <Onyx/Services/ProfileManager.h>
#include <Onyx/Types/TypeRegistry.h>
#include <Onyx/App/ViewerRegistry.h>
#include <Onyx/Viewers/DocumentWindow.h>
#include <Onyx/Services/Events.h>
#include <cstdio>
#include <cstdlib>

namespace Onyx::Api {

    static Onyx::Services::AssetDatabase*  s_database  = nullptr;
    static Onyx::Services::AppConfig*      s_config    = nullptr;
    static Onyx::App::ViewerRegistry* s_viewers   = nullptr;
    static Onyx::Viewers::DocumentWindow* s_documents = nullptr;

    static AssetEntry*         s_selectedEntry = nullptr;
    static AssetContainer*             s_selectedWad   = nullptr;
    static bool                s_selectionGuardsInstalled = false;

    void Init(const InitParams& params) {
        s_database  = params.db;
        s_config    = params.config;
        s_viewers   = params.viewers;
        s_documents = params.documents;

        // The selection is a pair of raw pointers into AssetDatabase::wads /
        // ::paks. Closing a container erases those vector elements, freeing the
        // AssetEntry storage the selection points at -- so every reader (the
        // Inspector's per-frame draw, App's copy-hash action, the browsers'
        // highlight test) would dereference freed memory on the very next
        // frame. Drop the selection while the pointers are still live:
        // AssetDatabase::CloseWad posts EventWadClosed *before* it erases.
        //
        // Clearing on any close mirrors DocumentWindow, which closes all tabs
        // for the same reason. Per-container selective clearing needs viewers
        // to track their source container first.
        if (!s_selectionGuardsInstalled) {
            s_selectionGuardsInstalled = true;
            EventWadClosed::subscribe(&s_selectedEntry,
                                      [](size_t) { SetSelected(nullptr, nullptr); });
            EventAllClosed::subscribe(&s_selectedEntry,
                                      []() { SetSelected(nullptr, nullptr); });
        }
    }

    Onyx::Services::AssetDatabase& Database() {
        if (!s_database) {
            fprintf(stderr, "[ToolkitApi] FATAL: Database() called before Init()\n");
            std::abort();
        }
        return *s_database;
    }

    Onyx::Services::AppConfig& Config() {
        if (!s_config) {
            fprintf(stderr, "[ToolkitApi] FATAL: Config() called before Init()\n");
            std::abort();
        }
        return *s_config;
    }

    Onyx::Services::ProfileManager& Profiles() {
        return Onyx::Services::ProfileManager::Get();
    }

    Onyx::Types::TypeRegistry& Types() {
        return Onyx::Types::TypeRegistry::Get();
    }

    Onyx::App::ViewerRegistry& Viewers() {
        if (!s_viewers) {
            fprintf(stderr, "[ToolkitApi] FATAL: Viewers() called before Init() or missing param\n");
            std::abort();
        }
        return *s_viewers;
    }

    Viewers::DocumentWindow& Documents() {
        if (!s_documents) {
            fprintf(stderr, "[ToolkitApi] FATAL: Documents() called before Init() or missing param\n");
            std::abort();
        }
        return *s_documents;
    }

    AssetEntry* GetSelected() {
        return s_selectedEntry;
    }

    AssetContainer* GetSelectedWad() {
        return s_selectedWad;
    }

    void SetSelected(AssetEntry* entry, AssetContainer* wad) {
        s_selectedEntry = entry;
        s_selectedWad   = wad;
        EventAssetSelected::post(entry, wad);
    }

} // namespace Onyx::Api
