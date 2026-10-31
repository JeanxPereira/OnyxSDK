#include "Onyx/Ui/Panels/SettingsPanel.h"

#include <Onyx/Core/Modules/Workspace.h>
#include <Onyx/Core/Services/Settings.h>
#include <Onyx/Ui/Theme.h>
#include <Onyx/Ui/UiContext.h>
#include <Onyx/Ui/Widgets.h>

#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <variant>

namespace Onyx::Ui {

namespace {

using Entry = Services::SettingsSchema::Entry;
using Kind = Services::SettingsSchema::Kind;

constexpr const char* kThemeMode = "ui.theme.mode";
constexpr const char* kThemeFontSize = "ui.theme.fontSizePt";
constexpr const char* kThemeScale = "ui.theme.userScale";
constexpr const char* kThemeFontPath = "ui.theme.fontPath";
constexpr const char* kThemeAccent = "ui.theme.accent";

const char* Label(const Entry& entry) { return entry.label.empty() ? entry.key.c_str() : entry.label.c_str(); }

bool EditText(const char* label, std::string& value) {
    char buffer[512];
    std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
    if (!ImGui::InputText(label, buffer, sizeof(buffer))) return false;
    value = buffer;
    return true;
}

void DrawThemeEntry(UiContext& ui, const Entry& entry) {
    const ThemeState& state = ui.theme.State();
    ImGui::PushID(entry.key.c_str());
    if (entry.key == kThemeMode) {
        int mode = static_cast<int>(state.mode);
        std::vector<const char*> names;
        for (const std::string& option : entry.options) names.push_back(option.c_str());
        if (ImGui::Combo(Label(entry), &mode, names.data(), static_cast<int>(names.size()))) {
            ui.theme.Mutate([mode](ThemeState& s) { s.mode = static_cast<ThemeMode>(mode); }, true);
        }
    } else if (entry.key == kThemeFontSize) {
        float value = state.fontSizePt;
        if (ImGui::SliderFloat(Label(entry), &value, static_cast<float>(entry.realMin),
                               static_cast<float>(entry.realMax), "%.1f")) {
            ui.theme.Mutate([value](ThemeState& s) { s.fontSizePt = value; });
        }
    } else if (entry.key == kThemeScale) {
        float value = state.userScale;
        if (ImGui::SliderFloat(Label(entry), &value, static_cast<float>(entry.realMin),
                               static_cast<float>(entry.realMax), "%.2f")) {
            ui.theme.Mutate([value](ThemeState& s) { s.userScale = value; });
        }
    } else if (entry.key == kThemeFontPath) {
        std::string value = state.fontPath;
        if (EditText(Label(entry), value)) {
            ui.theme.Mutate([value](ThemeState& s) { s.fontPath = value; });
        }
    } else if (entry.key == kThemeAccent) {
        float color[4] = {state.accent.x, state.accent.y, state.accent.z, state.accent.w};
        if (ImGui::ColorEdit4(Label(entry), color, ImGuiColorEditFlags_NoInputs)) {
            const glm::vec4 accent(color[0], color[1], color[2], color[3]);
            ui.theme.Mutate([accent](ThemeState& s) { s.accent = accent; }, true);
        }
    }
    ImGui::PopID();
}

void DrawModuleEntry(Services::Settings& settings, const Entry& entry) {
    ImGui::PushID(entry.key.c_str());
    switch (entry.kind) {
        case Kind::Bool: {
            bool value = settings.GetBool(entry.key).value_or(std::get<bool>(entry.def));
            if (ImGui::Checkbox(Label(entry), &value)) settings.Set(entry.key, value);
            break;
        }
        case Kind::Int: {
            int64_t value = settings.GetInt(entry.key).value_or(std::get<int64_t>(entry.def));
            const bool bounded = entry.max > entry.min;
            const bool changed = bounded ? ImGui::SliderScalar(Label(entry), ImGuiDataType_S64, &value,
                                                               &entry.min, &entry.max)
                                         : ImGui::InputScalar(Label(entry), ImGuiDataType_S64, &value);
            if (changed) settings.Set(entry.key, std::clamp(value, bounded ? entry.min : INT64_MIN,
                                                            bounded ? entry.max : INT64_MAX));
            break;
        }
        case Kind::Double: {
            double value = settings.GetDouble(entry.key).value_or(std::get<double>(entry.def));
            const bool bounded = entry.realMax > entry.realMin;
            const bool changed = bounded ? ImGui::SliderScalar(Label(entry), ImGuiDataType_Double, &value,
                                                               &entry.realMin, &entry.realMax)
                                         : ImGui::InputScalar(Label(entry), ImGuiDataType_Double, &value);
            if (changed) settings.Set(entry.key, value);
            break;
        }
        case Kind::String:
        case Kind::Path: {
            std::string value = settings.GetString(entry.key).value_or(std::get<std::string>(entry.def));
            if (EditText(Label(entry), value)) settings.Set(entry.key, value);
            break;
        }
        case Kind::Enum: {
            if (entry.options.empty()) break;
            const std::string fallback = entry.options[std::min(std::get<size_t>(entry.def), entry.options.size() - 1)];
            const std::string current = settings.GetString(entry.key).value_or(fallback);
            if (ImGui::BeginCombo(Label(entry), current.c_str())) {
                for (const std::string& option : entry.options) {
                    if (ImGui::Selectable(option.c_str(), option == current)) settings.Set(entry.key, option);
                }
                ImGui::EndCombo();
            }
            break;
        }
    }
    ImGui::PopID();
}

void ResetEntry(Services::Settings& settings, const Entry& entry) {
    switch (entry.kind) {
        case Kind::Bool: settings.Set(entry.key, std::get<bool>(entry.def)); break;
        case Kind::Int: settings.Set(entry.key, std::get<int64_t>(entry.def)); break;
        case Kind::Double: settings.Set(entry.key, std::get<double>(entry.def)); break;
        case Kind::String:
        case Kind::Path: settings.Set(entry.key, std::get<std::string>(entry.def)); break;
        case Kind::Enum:
            if (!entry.options.empty()) {
                settings.Set(entry.key, entry.options[std::min(std::get<size_t>(entry.def), entry.options.size() - 1)]);
            }
            break;
    }
}

}

SettingsModel SettingsPanel::Model(UiContext& ui) {
    SettingsModel model;

    Services::SettingsSchema appearance;
    Theme::DescribeSettings(appearance);
    model.tabs.push_back({"appearance", "Appearance", appearance.Entries()});

    for (Modules::IGameModule* module : ui.workspace.Modules()) {
        const Modules::ModuleInfo info = module->Info();
        model.tabs.push_back({info.id, info.displayName.empty() ? info.id : info.displayName,
                              ui.workspace.ModuleSettings(info.id).Entries()});
    }
    return model;
}

void SettingsPanel::Apply(UiContext& ui) {
    ui.theme.Save();
    ui.workspace.Settings().SaveIfDirty();
}

void SettingsPanel::Reset(UiContext& ui, const SettingsModel& model, size_t tab) {
    if (tab >= model.tabs.size()) return;
    if (tab == 0) {
        ui.theme.Set(ThemeState{});
        return;
    }
    for (const Entry& entry : model.tabs[tab].entries) ResetEntry(ui.workspace.Settings(), entry);
}

void SettingsPanel::Draw(UiContext& ui) {
    const size_t moduleCount = ui.workspace.Modules().size();
    if (moduleCount != m_moduleCount) {
        m_model = Model(ui);
        m_moduleCount = moduleCount;
    }

    if (ImGui::BeginTabBar("settingsTabs")) {
        for (size_t t = 0; t < m_model.tabs.size(); ++t) {
            const SettingsTab& tab = m_model.tabs[t];
            if (!ImGui::BeginTabItem(tab.title.c_str())) continue;
            for (const Entry& entry : tab.entries) {
                if (t == 0) DrawThemeEntry(ui, entry);
                else DrawModuleEntry(ui.workspace.Settings(), entry);
            }
            ImGui::Separator();
            if (Widgets::ToolbarButton("Apply")) Apply(ui);
            ImGui::SameLine();
            if (Widgets::ToolbarButton("Reset to defaults")) Reset(ui, m_model, t);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
}

}
