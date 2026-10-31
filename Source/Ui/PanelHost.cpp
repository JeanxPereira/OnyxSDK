#include "Onyx/Ui/PanelHost.h"

#include <Onyx/Core/Services/Log.h>
#include <Onyx/Ui/UiContext.h>

#include <exception>

namespace Onyx::Ui {

void PanelHost::Add(std::unique_ptr<IPanel> panel) {
    if (!panel) return;
    if (m_drawing) {
        m_pending.push_back({std::move(panel), {}});
        return;
    }
    m_reported.erase(std::string(panel->Id()));
    for (auto& existing : m_panels) {
        if (existing->Id() == panel->Id()) {
            existing = std::move(panel);
            return;
        }
    }
    m_panels.push_back(std::move(panel));
}

void PanelHost::Remove(std::string_view id) {
    if (m_drawing) {
        m_pending.push_back({nullptr, std::string(id)});
        return;
    }
    if (const auto it = m_reported.find(id); it != m_reported.end()) m_reported.erase(it);
    std::erase_if(m_panels, [&](const std::unique_ptr<IPanel>& p) { return p->Id() == id; });
}

IPanel* PanelHost::Find(std::string_view id) {
    for (auto& p : m_panels) {
        if (p->Id() == id) return p.get();
    }
    return nullptr;
}

void PanelHost::DrawAll(UiContext& ui) {
    m_drawing = true;
    for (size_t i = 0; i < m_panels.size(); ++i) {
        IPanel& panel = *m_panels[i];
        if (!panel.visible) continue;

        const std::string label = std::string(panel.Title()) + "##" + std::string(panel.Id());
        std::string failure;
        if (ImGui::Begin(label.c_str(), &panel.visible)) {
            try {
                panel.Draw(ui);
            } catch (const std::exception& e) {
                failure = e.what();
            } catch (...) {
                failure = "unknown exception";
            }
        }
        ImGui::End();

        if (!failure.empty()) {
            panel.visible = false;
            const std::string id(panel.Id());
            if (m_reported.insert(id).second)
                Services::Log::Default().Write(Services::LogLevel::Error, "ui.panel", "Panel '{}' threw: {}", id, failure);
        }
    }

    m_drawing = false;
    std::vector<Pending> pending;
    pending.swap(m_pending);
    for (Pending& op : pending) {
        if (op.add) Add(std::move(op.add));
        else Remove(op.remove);
    }
}

}
