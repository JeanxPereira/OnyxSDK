#include "Onyx/Ui/Panels/SettingsPanel.h"

#include <Onyx/Core/Modules/Workspace.h>
#include <Onyx/Ui/Theme.h>
#include <Onyx/Ui/UiContext.h>
#include <Onyx/Ui/Widgets.h>

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <variant>

namespace Onyx::Ui {

namespace {

using Entry = Services::SettingsSchema::Entry;
using Kind = Services::SettingsSchema::Kind;

const char* Label(const Entry& entry) { return entry.label.empty() ? entry.key.c_str() : entry.label.c_str(); }

template <class T>
T DefaultOr(const Entry& entry, T fallback) {
    const T* value = std::get_if<T>(&entry.def);
    return value ? *value : fallback;
}

std::string EnumDefault(const Entry& entry) {
    if (entry.options.empty()) return {};
    const size_t index = std::min(DefaultOr<size_t>(entry, 0), entry.options.size() - 1);
    return entry.options[index];
}

bool EditText(const char* label, std::string& value) {
    char buffer[512];
    std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
    if (!ImGui::InputText(label, buffer, sizeof(buffer))) return false;
    value = buffer;
    return true;
}

// Draws one entry of a schema over `settings` and writes a changed value back. Every Kind has a
// widget, so a schema entry is never skipped.
bool DrawEntry(Services::Settings& settings, const Entry& entry) {
    bool changed = false;
    ImGui::PushID(entry.key.c_str());
    switch (entry.kind) {
        case Kind::Bool: {
            bool value = settings.GetBool(entry.key).value_or(DefaultOr<bool>(entry, false));
            if (ImGui::Checkbox(Label(entry), &value)) {
                settings.Set(entry.key, value);
                changed = true;
            }
            break;
        }
        case Kind::Int: {
            int64_t value = settings.GetInt(entry.key).value_or(DefaultOr<int64_t>(entry, 0));
            const bool bounded = entry.max > entry.min;
            const bool edited = bounded ? ImGui::SliderScalar(Label(entry), ImGuiDataType_S64, &value,
                                                              &entry.min, &entry.max)
                                        : ImGui::InputScalar(Label(entry), ImGuiDataType_S64, &value);
            if (edited) {
                settings.Set(entry.key, std::clamp(value, bounded ? entry.min : INT64_MIN,
                                                   bounded ? entry.max : INT64_MAX));
                changed = true;
            }
            break;
        }
        case Kind::Double: {
            double value = settings.GetDouble(entry.key).value_or(DefaultOr<double>(entry, 0.0));
            const bool bounded = entry.realMax > entry.realMin;
            const bool edited = bounded ? ImGui::SliderScalar(Label(entry), ImGuiDataType_Double, &value,
                                                              &entry.realMin, &entry.realMax)
                                        : ImGui::InputScalar(Label(entry), ImGuiDataType_Double, &value);
            if (edited) {
                settings.Set(entry.key, value);
                changed = true;
            }
            break;
        }
        case Kind::String:
        case Kind::Path: {
            std::string value = settings.GetString(entry.key).value_or(DefaultOr<std::string>(entry, {}));
            if (EditText(Label(entry), value)) {
                settings.Set(entry.key, value);
                changed = true;
            }
            break;
        }
        case Kind::Enum: {
            if (entry.options.empty()) break;
            const std::string current = settings.GetString(entry.key).value_or(EnumDefault(entry));
            if (ImGui::BeginCombo(Label(entry), current.c_str())) {
                for (const std::string& option : entry.options) {
                    if (ImGui::Selectable(option.c_str(), option == current)) {
                        settings.Set(entry.key, option);
                        changed = true;
                    }
                }
                ImGui::EndCombo();
            }
            break;
        }
    }
    ImGui::PopID();
    return changed;
}

void ResetEntry(Services::Settings& settings, const Entry& entry) {
    switch (entry.kind) {
        case Kind::Bool: settings.Set(entry.key, DefaultOr<bool>(entry, false)); break;
        case Kind::Int: settings.Set(entry.key, DefaultOr<int64_t>(entry, 0)); break;
        case Kind::Double: settings.Set(entry.key, DefaultOr<double>(entry, 0.0)); break;
        case Kind::String:
        case Kind::Path: settings.Set(entry.key, DefaultOr<std::string>(entry, {})); break;
        case Kind::Enum:
            if (!entry.options.empty()) settings.Set(entry.key, EnumDefault(entry));
            break;
    }
}

}

SettingsModel SettingsPanel::Model(UiContext& ui) {
    SettingsModel model;

    Services::SettingsSchema appearance;
    Theme::DescribeSettings(appearance);
    model.tabs.push_back({"appearance", "Appearance", "Appearance###appearance", appearance.Entries()});

    for (Modules::IGameModule* module : ui.workspace.Modules()) {
        const Modules::ModuleInfo info = module->Info();
        const std::string title = info.displayName.empty() ? info.id : info.displayName;
        model.tabs.push_back({info.id, title, title + "###" + info.id,
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
    if (!m_appearanceBuilt || !(m_appearanceFor == ui.theme.State())) {
        m_appearanceFor = ui.theme.State();
        m_appearance = Services::Settings::Empty();
        Theme::WriteState(m_appearance, m_appearanceFor);
        m_appearanceBuilt = true;
    }

    if (ImGui::BeginTabBar("settingsTabs")) {
        for (size_t t = 0; t < m_model.tabs.size(); ++t) {
            const SettingsTab& tab = m_model.tabs[t];
            if (!ImGui::BeginTabItem(tab.label.c_str())) continue;

            bool appearanceChanged = false;
            for (const Entry& entry : tab.entries) {
                if (t == 0) appearanceChanged = DrawEntry(m_appearance, entry) || appearanceChanged;
                else DrawEntry(ui.workspace.Settings(), entry);
            }
            if (appearanceChanged) {
                m_appearanceFor = Theme::ReadState(m_appearance);
                ui.theme.Set(m_appearanceFor);
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
