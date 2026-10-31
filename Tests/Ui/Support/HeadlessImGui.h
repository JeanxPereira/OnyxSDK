#pragma once

#include <imgui.h>

// An ImGui context with no window and no renderer. It plays the backend's part of the 1.92
// texture protocol: every texture the context asks for is "created" or "updated" by marking it
// OK, so fonts rasterise on demand without a GPU.
struct HeadlessImGui {
    HeadlessImGui() {
        IMGUI_CHECKVERSION();
        ctx = ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1280.0f, 720.0f);
        io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
        io.Fonts->AddFontDefault();
    }

    ~HeadlessImGui() { ImGui::DestroyContext(ctx); }

    HeadlessImGui(const HeadlessImGui&) = delete;
    HeadlessImGui& operator=(const HeadlessImGui&) = delete;

    void NewFrame(float dt = 1.0f / 60) {
        ImGui::GetIO().DeltaTime = dt;
        ImGui::NewFrame();
    }

    void EndFrame() {
        ImGui::Render();
        for (ImTextureData* tex : ImGui::GetPlatformIO().Textures) {
            switch (tex->Status) {
            case ImTextureStatus_WantCreate:
                tex->SetTexID(static_cast<ImTextureID>(++m_nextTexture));
                tex->SetStatus(ImTextureStatus_OK);
                break;
            case ImTextureStatus_WantUpdates:
                tex->SetStatus(ImTextureStatus_OK);
                break;
            case ImTextureStatus_WantDestroy:
                tex->SetTexID(ImTextureID_Invalid);
                tex->SetStatus(ImTextureStatus_Destroyed);
                break;
            default:
                break;
            }
        }
    }

    ImGuiContext* ctx = nullptr;

private:
    ImU64 m_nextTexture = 0;
};
