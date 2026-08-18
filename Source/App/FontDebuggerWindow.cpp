#include "FontDebuggerWindow.h"

#include <Onyx/Fonts/IconTable.h>

#include <imgui.h>
#include <imgui_internal.h>

#include <cstdio>

namespace Onyx::App {

// The icon list itself is generated from SFSymbols.h at build time -- see
// Onyx::Fonts::IconTable(). This window is the deep view: it reports whether a
// glyph really came from the SF Symbols atlas or from a fallback font, plus its
// metrics and a zoomed render straight out of the atlas texture.

void FontDebuggerWindow::Draw(bool* p_open) {
    if (!*p_open) return;

    const Onyx::Fonts::IconEntry* icons = Onyx::Fonts::IconTable();
    const int iconCount = Onyx::Fonts::IconCount();

    ImGui::SetNextWindowSize(ImVec2(800, 500), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("SF Symbols Debugger", p_open)) {
        static ImGuiTextFilter filter;
        filter.Draw("Filter Icons");
        
        ImGui::Columns(2, "IconColumns", true);
        
        static int selected_idx = -1;

        ImGui::BeginChild("IconList");
        for (int i = 0; i < iconCount; ++i) {
            const auto& icon = icons[i];
            if (filter.PassFilter(icon.name)) {
                ImGui::PushID(i);
                bool is_selected = (selected_idx == i);
                
                char label[256];
                snprintf(label, sizeof(label), "%s %s", icon.value, icon.name);
                
                if (ImGui::Selectable(label, is_selected)) {
                    selected_idx = i;
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild();
        
        ImGui::NextColumn();
        
        ImGui::BeginChild("IconDetails");
        if (selected_idx >= 0 && selected_idx < iconCount) {
            const auto& icon = icons[selected_idx];
            ImGui::Text("Name: %s", icon.name);
            ImGui::Text("Icon: %s", icon.value);
            
            // Decode utf8
            unsigned int c = 0;
            ImTextCharFromUtf8(&c, icon.value, nullptr);
            ImGui::Text("Codepoint: U+%04X", c);
            
            ImGui::Separator();
            ImGui::Text("stb_truetype & ImGui Glyph Info:");
            
            ImFont* font = ImGui::GetFont();
            ImFontBaked* baked = font->GetFontBaked(ImGui::GetFontSize());
            const ImFontGlyph* glyph = baked ? baked->FindGlyph((ImWchar)c) : nullptr;
            const ImFontGlyph* glyph_nofallback = baked ? baked->FindGlyphNoFallback((ImWchar)c) : nullptr;
            
            if (!glyph) {
                ImGui::TextColored(ImVec4(1,0,0,1), "ERROR: Glyph completely missing!");
            } else {
                if (!glyph_nofallback) {
                    ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "WARNING: Glyph missing in primary SFSymbols font!");
                    ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "ImGui used the fallback font for this codepoint (e.g. rendered from Proggy or Inter instead).");
                    ImGui::TextColored(ImVec4(1, 0.5f, 0, 1), "This is why you see characters like 'ҫ' instead of the icon.");
                } else {
                    ImGui::TextColored(ImVec4(0, 1, 0, 1), "Glyph found in SFSymbols natively (No fallback).");
                }
                
                ImGui::Spacing();
                ImGui::Text("AdvanceX: %.2f", glyph->AdvanceX);
                ImGui::Text("Bounds: (%.1f, %.1f) - (%.1f, %.1f)", glyph->X0, glyph->Y0, glyph->X1, glyph->Y1);
                ImGui::Text("UV Coords in Atlas:");
                ImGui::Text("  U0: %f, V0: %f", glyph->U0, glyph->V0);
                ImGui::Text("  U1: %f, V1: %f", glyph->U1, glyph->V1);
                
                // Draw huge preview via texture atlas
                ImGui::Spacing();
                ImGui::Text("Atlas Render Preview:");
                ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f), 
                    "(Note: Displaying zoomed-in image from the actual GPU buffer. \nLow resolution because the actual font texture is baked at UI text size (~15px).)");
                ImFontAtlas* atlas = font->OwnerAtlas;
                if (atlas && atlas->TexData) {
                    ImVec2 rect_size((glyph->U1 - glyph->U0) * atlas->TexData->Width,
                                     (glyph->V1 - glyph->V0) * atlas->TexData->Height);
                                     
                    // Scale it up
                    float scale = 4.0f;
                    ImGui::Image((ImTextureID)atlas->TexData->GetTexID(), 
                                 ImVec2(rect_size.x * scale, rect_size.y * scale),
                                 ImVec2(glyph->U0, glyph->V0), ImVec2(glyph->U1, glyph->V1),
                                 ImVec4(1,1,1,1), ImVec4(1,1,1,1));
                }
            }
        } else {
            ImGui::Text("Select an icon to view debug details.");
        }
        ImGui::EndChild();
        
        ImGui::Columns(1);
    }
    ImGui::End();
}

} // namespace Onyx::App
