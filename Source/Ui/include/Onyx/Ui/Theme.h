#pragma once

#include <functional>

#include "Onyx/Ui/ThemeState.h"

namespace Onyx::Services {
class Settings;
class SettingsSchema;
}

namespace Onyx::Ui {

// The only writer of ImGui::GetStyle(). Panels express intent through Mutate;
// the host calls Commit once per frame, outside NewFrame..Render.
class Theme {
public:
    explicit Theme(Services::Settings& settings);

    const ThemeState& State() const { return m_state; }
    const ThemeEnvironment& Env() const { return m_env; }
    const ThemeResolved& Applied() const { return m_applied; }

    void Mutate(const std::function<void(ThemeState&)>& fn, bool animateColors = false);
    void Set(const ThemeState& state);
    void SetEnvironment(const ThemeEnvironment& env);

    void Commit();
    void Update(float dt);

    bool AtlasDirty() const { return m_atlasDirty; }
    void ClearAtlasDirty() { m_atlasDirty = false; }
    bool IsTransitioning() const { return m_transitioning; }

    // Writes ui.theme.* into the Settings object; the host decides when to write the file.
    void Save();

    static void DescribeSettings(Services::SettingsSchema& schema);

private:
    void NoteAtlasInputs(const ThemeResolved& resolved);

    Services::Settings& m_settings;
    ThemeState m_state;
    ThemeEnvironment m_env;
    ThemeResolved m_applied;
    bool m_hasApplied = false;
    bool m_dirty = true;
    bool m_animateNext = false;

    std::string m_atlasPath;
    float m_atlasRefPx = 0.0f;
    bool m_atlasDirty = false;

    bool m_transitioning = false;
    float m_elapsed = 0.0f;
    ImVec4 m_from[ImGuiCol_COUNT]{};
};

}
