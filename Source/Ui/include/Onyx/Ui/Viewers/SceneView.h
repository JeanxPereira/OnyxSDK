#pragma once

#include <Onyx/Ui/ViewerRegistry.h>

#include <Onyx/Core/Domain/Scene.h>
#include <Onyx/Core/Result.h>
#include <Onyx/Core/Services/Diagnostics.h>
#include <Onyx/Gpu/Upload.h>
#include <Onyx/Render/SceneRenderer.h>
#include <Onyx/Ui/Panel.h>
#include <Onyx/Ui/SceneSession.h>

#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace Onyx::Ui {

struct UiContext;

// A mesh document: the decoded scene, the session that shows it in the Viewport panel, a toolbar
// (shading, grid, bones, frame) and the list of drawn parts. The scene reaches the GPU when the
// Viewport calls Load with its renderer; until then the session is not Loaded.
class SceneView : public IDocumentView {
public:
    SceneView(Modules::DocumentId doc, Modules::NodePath node, std::string name, std::unique_ptr<Domain::Scene> scene);

    SceneView(const SceneView&) = delete;
    SceneView& operator=(const SceneView&) = delete;

    std::string_view Id() const override { return m_id; }
    std::string_view Title() const override { return m_name; }
    void Draw(UiContext& ui) override;
    Modules::DocumentId Document() const override { return m_doc; }
    Modules::NodePath Node() const override { return m_node; }

    const Domain::Scene& Source() const { return *m_scene; }
    SceneSession& Session() { return m_session; }
    const SceneSession& Session() const { return m_session; }

    // Uploads the scene once; a failure is kept in Fault and not retried.
    Result<void> Load(Render::SceneRenderer& renderer, Gpu::UploadContext& upload, Services::DiagSink* diags);
    const std::optional<Error>& Fault() const { return m_fault; }

private:
    Modules::DocumentId m_doc;
    Modules::NodePath m_node;
    std::string m_name;
    std::string m_id;
    std::unique_ptr<Domain::Scene> m_scene;
    SceneSession m_session;
    std::optional<Error> m_fault;
};

// The active tab of the "onyx.documents" panel when it is a SceneView; null otherwise.
SceneView* ActiveSceneView(UiContext& ui);

// Null when the request holds no scene.
std::unique_ptr<IDocumentView> CreateSceneView(OpenRequest& request);

// Registers onyx.scene for Domain::MediaKind::Mesh at priority 0.
void RegisterSceneViewer(ViewerRegistry& registry);

}
