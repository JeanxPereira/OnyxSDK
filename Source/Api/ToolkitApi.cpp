#include <Onyx/Api/ToolkitApi.h>
#include <Onyx/Types/TypeRegistry.h>
#include <Onyx/App/ViewerRegistry.h>
#include <Onyx/Viewers/DocumentWindow.h>
#include <cstdio>
#include <cstdlib>

namespace Onyx::Api {

    static Onyx::Services::AppConfig*      s_config    = nullptr;
    static Onyx::App::ViewerRegistry* s_viewers   = nullptr;
    static Onyx::Viewers::DocumentWindow* s_documents = nullptr;

    void Init(const InitParams& params) {
        s_config    = params.config;
        s_viewers   = params.viewers;
        s_documents = params.documents;
    }

    Onyx::Services::AppConfig& Config() {
        if (!s_config) {
            fprintf(stderr, "[ToolkitApi] FATAL: Config() called before Init()\n");
            std::abort();
        }
        return *s_config;
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

} // namespace Onyx::Api
