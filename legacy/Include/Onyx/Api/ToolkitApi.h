#pragma once

// ToolkitApi — Global singleton facade for accessing core subsystems.
// Inspired by ImHexApi::System — provides global access without AppContext& pass-through.

namespace Onyx::Services {
    struct AppConfig;
}
namespace Onyx::App {
    class ViewerRegistry;
}
namespace Onyx::Viewers {
    class DocumentWindow;
}
namespace Onyx::Types { class TypeRegistry; }

namespace Onyx::Api {

    struct InitParams {
        Onyx::Services::AppConfig*             config = nullptr;
        Onyx::App::ViewerRegistry*             viewers = nullptr;
        Onyx::Viewers::DocumentWindow*         documents = nullptr;
    };

    /// Initialize the facade pointers. Call once in App::init().
    void Init(const InitParams& params);

    /// Access the global AppConfig.
    Onyx::Services::AppConfig& Config();

    /// Access the global TypeRegistry.
    Onyx::Types::TypeRegistry& Types();

    /// Access the global ViewerRegistry.
    Onyx::App::ViewerRegistry& Viewers();

    /// Access the global DocumentWindow.
    Viewers::DocumentWindow& Documents();

} // namespace Onyx::Api
