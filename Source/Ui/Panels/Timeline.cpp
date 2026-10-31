#include "Onyx/Ui/Panels/Timeline.h"

#include <Onyx/Ui/FrameClock.h>
#include <Onyx/Ui/UiContext.h>
#include <Onyx/Ui/Viewers/SceneView.h>
#include <Onyx/Ui/Widgets.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Onyx::Ui {

namespace {

constexpr float kTrackHeight = 36.0f;
constexpr float kFrameRate = 30.0f;
constexpr float kMinTickGap = 5.0f;
constexpr float kMinLabelGap = 48.0f;

}

float Timeline::TimeAt(float x, float left, float right, float duration) {
    if (right <= left || duration <= 0.0f) return 0.0f;
    return std::clamp((x - left) / (right - left), 0.0f, 1.0f) * duration;
}

float Timeline::PixelAt(float time, float left, float right, float duration) {
    if (duration <= 0.0f) return left;
    return left + (right - left) * (time / duration);
}

void Timeline::Draw(UiContext& ui) {
    SceneView* view = ActiveSceneView(ui);
    if (!view || !view->Session().Player().HasClip()) {
        Widgets::Hint("No animation to scrub.");
        return;
    }
    SceneSession& session = view->Session();
    const float duration = session.Player().Duration();
    if (duration <= 0.0f) {
        Widgets::Hint("The clip has no length.");
        return;
    }

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const ImVec2 size(std::max(avail.x, 1.0f), std::min(std::max(avail.y, kTrackHeight), kTrackHeight * 2.0f));
    ImGui::InvisibleButton("##track", size);
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();

    if (ImGui::IsItemActivated()) session.Pause();
    if (ImGui::IsItemActive()) {
        session.Seek(TimeAt(ImGui::GetIO().MousePos.x, min.x, max.x, duration));
        ui.clock.RequestRedraw();
    }

    ImDrawList* list = ImGui::GetWindowDrawList();
    list->AddRectFilled(min, max, ImGui::GetColorU32(ImGuiCol_FrameBg), 3.0f);

    // A tick per frame of the display rate while they are at least kMinTickGap apart, and a taller,
    // labelled one per second, thinned until the labels clear each other.
    const float width = max.x - min.x;
    const ImU32 tick = ImGui::GetColorU32(ImGuiCol_Border);
    const ImU32 text = ImGui::GetColorU32(ImGuiCol_TextDisabled);
    if (width / (duration * kFrameRate) >= kMinTickGap) {
        const int frames = static_cast<int>(std::floor(duration * kFrameRate));
        for (int f = 0; f <= frames; ++f) {
            const float x = PixelAt(static_cast<float>(f) / kFrameRate, min.x, max.x, duration);
            list->AddLine(ImVec2(x, max.y - (max.y - min.y) * 0.3f), ImVec2(x, max.y), tick);
        }
    }
    const int seconds = static_cast<int>(std::floor(duration));
    const int labelStep = std::max(1, static_cast<int>(std::ceil(kMinLabelGap / (width / duration))));
    for (int s = 0; s <= seconds; s += labelStep) {
        const float x = PixelAt(static_cast<float>(s), min.x, max.x, duration);
        char label[16];
        std::snprintf(label, sizeof(label), "%ds", s);
        list->AddLine(ImVec2(x, min.y), ImVec2(x, max.y), tick);
        list->AddText(ImVec2(x + 3.0f, min.y), text, label);
    }

    const float head = PixelAt(session.Player().Time(), min.x, max.x, duration);
    const ImU32 accent = ImGui::GetColorU32(ImGuiCol_SliderGrab);
    list->AddLine(ImVec2(head, min.y), ImVec2(head, max.y), accent, 2.0f);
    list->AddTriangleFilled(ImVec2(head - 5.0f, min.y), ImVec2(head + 5.0f, min.y), ImVec2(head, min.y + 7.0f),
                            accent);
}

}
