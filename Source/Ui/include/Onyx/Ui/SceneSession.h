#pragma once

#include <Onyx/Core/Domain/Scene.h>
#include <Onyx/Core/Result.h>
#include <Onyx/Core/Services/Diagnostics.h>
#include <Onyx/Gpu/Upload.h>
#include <Onyx/Render/AnimationPlayer.h>
#include <Onyx/Render/Camera.h>
#include <Onyx/Render/GpuScene.h>
#include <Onyx/Render/SceneRenderer.h>
#include <Onyx/Render/View.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Onyx::Ui {

// The state of one open scene: the GPU copy, the camera, the animation player and the overlay
// switches. Scene() needs Loaded(). Every mutator that changes the
// pose copies the player's palette and joint worlds into the scene, so the next draw shows it.
class SceneSession {
public:
    // Uploads `scene` through `renderer` and frames its bounds. Selects the first clip when the scene
    // has a skeleton and animations. A session that is already Loaded keeps its GPU copy and returns Ok,
    // so a live GpuScene is never replaced while a frame may still draw it.
    Result<void> Load(Render::SceneRenderer& renderer, Gpu::UploadContext& upload, const Domain::Scene& scene,
                      Services::DiagSink* diags);
    bool Loaded() const { return m_scene.has_value(); }
    // Takes the skeleton and clips of `scene` without any GPU copy; Load calls it. With it alone the
    // transport state is live but nothing is drawn.
    void BindAnimation(const Domain::Scene& scene);

    Render::GpuScene& Scene() { return *m_scene; }
    const Render::GpuScene& Scene() const { return *m_scene; }
    Render::Camera& Camera() { return m_camera; }
    Render::AnimationPlayer& Player() { return m_player; }
    const Render::AnimationPlayer& Player() const { return m_player; }
    Render::Overlays& Overlays() { return m_overlays; }
    Render::ShadingMode& Mode() { return m_mode; }

    bool HasAnimation() const;
    bool HasSkeleton() const;
    const std::vector<std::string>& ClipNames() const { return m_clipNames; }

    // Advances the player and the camera tween by dt seconds. Only the Viewport calls it, so playback
    // runs while a Viewport draws this session and pauses in time while none does.
    void Tick(float dt);
    // True while the pose or the camera is still changing by itself.
    bool Animating() const;

    void SelectClip(uint32_t clip);
    void Play();
    void Pause();
    void Stop();
    void Seek(float seconds);
    void SetSpeed(float speed);
    void SetLoop(Render::LoopMode loop);
    float Speed() const { return m_speed; }
    Render::LoopMode Loop() const { return m_loop; }

private:
    void Push();

    std::shared_ptr<const Domain::Skeleton> m_skeleton;
    std::shared_ptr<const Domain::AnimationSet> m_animations;
    std::vector<std::string> m_clipNames;
    std::optional<Render::GpuScene> m_scene;
    Render::Camera m_camera;
    Render::AnimationPlayer m_player;
    Render::Overlays m_overlays{true};
    Render::ShadingMode m_mode = Render::ShadingMode::Solid;
    float m_speed = 1.0f;
    Render::LoopMode m_loop = Render::LoopMode::Loop;
};

}
