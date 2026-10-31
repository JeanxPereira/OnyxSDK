#include "Onyx/Ui/Panels/Transport.h"

#include <Onyx/Ui/FrameClock.h>
#include <Onyx/Ui/SfSymbols.h>
#include <Onyx/Ui/UiContext.h>
#include <Onyx/Ui/Viewers/SceneView.h>
#include <Onyx/Ui/Widgets.h>

#include <imgui.h>

#include <array>
#include <cstddef>
#include <cstdio>

namespace Onyx::Ui {

namespace {

constexpr std::array<float, 5> kSpeeds = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f};
constexpr std::array<const char*, 3> kLoopNames = {"Once", "Loop", "Ping-pong"};
constexpr std::array<Render::LoopMode, 3> kLoops = {Render::LoopMode::None, Render::LoopMode::Loop,
                                                    Render::LoopMode::PingPong};

}

Transport::Model Transport::Read(const SceneSession& session) {
    Model model;
    model.available = session.HasAnimation() && session.Player().HasClip();
    if (!model.available) return model;
    const Render::AnimationPlayer& player = session.Player();
    model.playing = player.IsPlaying();
    model.clips = std::span<const std::string>(session.ClipNames());
    model.clip = player.Clip();
    model.time = player.Time();
    model.duration = player.Duration();
    model.speed = session.Speed();
    model.loop = session.Loop();
    return model;
}

void Transport::Draw(UiContext& ui) {
    SceneView* view = ActiveSceneView(ui);
    if (!view) {
        Widgets::Hint("Open a mesh to control its animation.");
        return;
    }
    SceneSession& session = view->Session();
    const Model model = Read(session);
    if (!model.available) {
        Widgets::Hint("This scene has no animation.");
        return;
    }

    bool changed = false;
    {
        Widgets::IconButtonOptions options;
        options.tooltip = model.playing ? "Pause" : "Play";
        if (Widgets::IconButton("##play", model.playing ? ICON_SF_PAUSE : ICON_SF_PLAY, options)) {
            if (model.playing) {
                session.Pause();
            } else {
                session.Play();
            }
            changed = true;
        }
    }
    ImGui::SameLine();
    {
        Widgets::IconButtonOptions options;
        options.tooltip = "Stop";
        if (Widgets::IconButton("##stop", ICON_SF_STOP, options)) {
            session.Pause();
            session.Stop();
            changed = true;
        }
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::BeginCombo("##clip", model.clips[model.clip < model.clips.size() ? model.clip : 0].c_str())) {
        for (size_t i = 0; i < model.clips.size(); ++i) {
            const bool selected = i == model.clip;
            if (ImGui::Selectable(model.clips[i].c_str(), selected) && !selected) {
                session.SelectClip(static_cast<uint32_t>(i));
                changed = true;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(72.0f);
    char speedLabel[16];
    std::snprintf(speedLabel, sizeof(speedLabel), "%.2gx", static_cast<double>(model.speed));
    if (ImGui::BeginCombo("##speed", speedLabel)) {
        for (const float speed : kSpeeds) {
            char label[16];
            std::snprintf(label, sizeof(label), "%.2gx", static_cast<double>(speed));
            if (ImGui::Selectable(label, speed == model.speed)) {
                session.SetSpeed(speed);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(96.0f);
    size_t loopIndex = 1;
    for (size_t i = 0; i < kLoops.size(); ++i) {
        if (kLoops[i] == model.loop) loopIndex = i;
    }
    if (ImGui::BeginCombo("##loop", kLoopNames[loopIndex])) {
        for (size_t i = 0; i < kLoops.size(); ++i) {
            if (ImGui::Selectable(kLoopNames[i], i == loopIndex)) {
                session.SetLoop(kLoops[i]);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();
    ImGui::Text("%.2f / %.2f s", static_cast<double>(model.time), static_cast<double>(model.duration));

    if (changed) ui.clock.RequestRedraw();
}

}
