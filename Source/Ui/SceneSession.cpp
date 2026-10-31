#include "Onyx/Ui/SceneSession.h"

#include <utility>

namespace Onyx::Ui {

Result<void> SceneSession::Load(Render::SceneRenderer& renderer, Gpu::UploadContext& upload,
                                const Domain::Scene& scene, Services::DiagSink* diags) {
    if (m_scene) return Result<void>::Ok();
    auto loaded = renderer.Load(scene, upload, diags);
    if (!loaded.ok()) return Result<void>::Fail(loaded.error());
    m_scene.emplace(std::move(loaded.value()));
    m_camera.FocusOn(m_scene->Bounds());
    BindAnimation(scene);
    return Result<void>::Ok();
}

void SceneSession::BindAnimation(const Domain::Scene& scene) {
    m_skeleton = scene.skeleton;
    m_animations = scene.animations;
    m_clipNames.clear();
    if (m_animations) {
        for (const Domain::AnimClip& clip : m_animations->clips) m_clipNames.push_back(clip.name);
    }
    m_player = Render::AnimationPlayer();
    if (HasAnimation()) {
        m_player.Set(m_skeleton, m_animations, 0);
        m_player.SetSpeed(m_speed);
        m_player.SetLoop(m_loop);
    }
    Push();
}

bool SceneSession::HasSkeleton() const {
    return m_skeleton && !m_skeleton->joints.empty();
}

bool SceneSession::HasAnimation() const {
    return HasSkeleton() && m_animations && !m_animations->clips.empty();
}

void SceneSession::Push() {
    if (!m_scene || !m_player.HasClip()) return;
    m_scene->SetPalette(m_player.Palette());
    m_scene->SetWorldTransforms(m_player.WorldTransforms());
}

void SceneSession::Tick(float dt) {
    m_camera.UpdateAnimation(dt);
    if (m_player.Advance(dt)) Push();
}

bool SceneSession::Animating() const { return m_player.IsPlaying() || m_camera.IsAnimating(); }

void SceneSession::SelectClip(uint32_t clip) {
    if (!HasAnimation()) return;
    const bool playing = m_player.IsPlaying();
    m_player.Set(m_skeleton, m_animations, clip);
    m_player.SetPlaying(playing && m_player.HasClip());
    Push();
}

void SceneSession::Play() {
    if (m_player.HasClip()) m_player.SetPlaying(true);
}

void SceneSession::Pause() { m_player.SetPlaying(false); }

void SceneSession::Stop() {
    if (!m_player.HasClip()) return;
    m_player.Stop();
    Push();
}

void SceneSession::Seek(float seconds) {
    if (!m_player.HasClip()) return;
    m_player.SetTime(seconds);
    Push();
}

void SceneSession::SetSpeed(float speed) {
    m_speed = speed;
    m_player.SetSpeed(speed);
}

void SceneSession::SetLoop(Render::LoopMode loop) {
    m_loop = loop;
    m_player.SetLoop(loop);
}

}
