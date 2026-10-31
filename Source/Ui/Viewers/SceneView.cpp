#include "Onyx/Ui/Viewers/SceneView.h"

#include <Onyx/Ui/SfSymbols.h>
#include <Onyx/Ui/PanelHost.h>
#include <Onyx/Ui/UiContext.h>
#include <Onyx/Ui/Viewers/DocumentTabs.h>
#include <Onyx/Ui/Widgets.h>
#include <Onyx/Ui/FrameClock.h>

#include <imgui.h>

#include <cstdio>
#include <utility>

namespace Onyx::Ui {

SceneView::SceneView(Modules::DocumentId doc, Modules::NodePath node, std::string name,
                     std::unique_ptr<Domain::Scene> scene)
    : m_doc(doc),
      m_node(std::move(node)),
      m_name(std::move(name)),
      m_id("onyx.scene." + std::to_string(doc) + "." + m_name),
      m_scene(std::move(scene)) {}

Result<void> SceneView::Load(Render::SceneRenderer& renderer, Gpu::UploadContext& upload,
                             Services::DiagSink* diags) {
    if (m_fault) return Result<void>::Fail(*m_fault);
    if (m_session.Loaded()) return Result<void>::Ok();
    auto loaded = m_session.Load(renderer, upload, *m_scene, diags);
    if (!loaded.ok()) m_fault = loaded.error();
    return loaded;
}

void SceneView::Draw(UiContext& ui) {
    if (m_fault) {
        Widgets::StatusText(Services::Severity::Error, (m_fault->code + ": " + m_fault->message).c_str());
        return;
    }
    if (!m_session.Loaded()) {
        Widgets::Hint("The scene uploads when the Viewport panel is visible.");
        return;
    }

    bool changed = false;
    {
        Widgets::IconButtonOptions options;
        options.tooltip = m_session.Mode() == Render::ShadingMode::Textured ? "Shading: textured"
                                                                              : "Shading: solid";
        if (Widgets::IconButton("##shading", ICON_SF_CUBE, options)) {
            m_session.Mode() = m_session.Mode() == Render::ShadingMode::Solid ? Render::ShadingMode::Textured
                                                                              : Render::ShadingMode::Solid;
            changed = true;
        }
    }
    ImGui::SameLine();
    {
        Widgets::IconButtonOptions options;
        options.tooltip = "Grid";
        options.selected = m_session.Overlays().grid;
        if (Widgets::IconButton("##grid", ICON_SF_SQUARE_GRID_3X3, options)) {
            m_session.Overlays().grid = !m_session.Overlays().grid;
            changed = true;
        }
    }
    if (m_session.HasSkeleton()) {
        ImGui::SameLine();
        Widgets::IconButtonOptions options;
        options.tooltip = "Bones";
        options.selected = m_session.Overlays().skeleton;
        if (Widgets::IconButton("##bones", ICON_SF_FIGURE_WALK, options)) {
            m_session.Overlays().skeleton = !m_session.Overlays().skeleton;
            changed = true;
        }
    }
    ImGui::SameLine();
    {
        Widgets::IconButtonOptions options;
        options.tooltip = "Frame the scene";
        if (Widgets::IconButton("##frame", ICON_SF_VIEWFINDER, options)) {
            m_session.Camera().FocusOn(m_session.Scene().Bounds());
            changed = true;
        }
    }

    const std::span<const Render::DrawInfo> draws = m_session.Scene().Draws();
    uint64_t vertices = 0;
    uint64_t triangles = 0;
    for (const Render::DrawInfo& d : draws) {
        vertices += d.vertexCount;
        triangles += d.triangleCount;
    }
    ImGui::SameLine();
    char stats[96];
    std::snprintf(stats, sizeof(stats), "%zu parts, %llu verts, %llu tris", draws.size(),
                  static_cast<unsigned long long>(vertices), static_cast<unsigned long long>(triangles));
    Widgets::Hint(stats);
    if (changed) ui.clock.RequestRedraw();
    ImGui::Separator();

    constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
                                      ImGuiTableFlags_BordersInnerV;
    if (!ImGui::BeginTable("##parts", 5, flags)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Part", ImGuiTableColumnFlags_WidthStretch);
    ImGui::TableSetupColumn("Verts", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Tris", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Material", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableSetupColumn("Kind", ImGuiTableColumnFlags_WidthFixed);
    ImGui::TableHeadersRow();
    Widgets::ForEachVisible(draws.size(), ImGui::GetTextLineHeightWithSpacing(), [&](size_t i) {
        const Render::DrawInfo& d = draws[i];
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(d.name.c_str());
        ImGui::TableNextColumn();
        ImGui::Text("%u", d.vertexCount);
        ImGui::TableNextColumn();
        ImGui::Text("%u", d.triangleCount);
        ImGui::TableNextColumn();
        ImGui::Text("%u", d.materialIndex);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(d.sky ? "sky" : d.skinned ? "skinned" : "static");
    });
    ImGui::EndTable();
}

SceneView* ActiveSceneView(UiContext& ui) {
    auto* tabs = dynamic_cast<DocumentTabs*>(ui.panels.Find("onyx.documents"));
    return tabs ? dynamic_cast<SceneView*>(tabs->Active()) : nullptr;
}

std::unique_ptr<IDocumentView> CreateSceneView(OpenRequest& request) {
    if (!request.scene) return nullptr;
    return std::make_unique<SceneView>(request.doc, std::move(request.node), std::move(request.name),
                                       std::move(request.scene));
}

void RegisterSceneViewer(ViewerRegistry& registry) {
    ViewerSpec spec;
    spec.id = "onyx.scene";
    spec.target = Domain::MediaKind::Mesh;
    spec.priority = 0;
    spec.create = CreateSceneView;
    registry.Register(std::move(spec));
}

}
