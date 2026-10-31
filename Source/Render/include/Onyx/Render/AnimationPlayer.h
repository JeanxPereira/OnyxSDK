#pragma once

#include <Onyx/Core/Domain/Animation.h>
#include <Onyx/Core/Domain/Skeleton.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <span>
#include <vector>

namespace Onyx::Render {

enum class LoopMode : uint8_t { None, Loop, PingPong };

// Samples an AnimClip into a joint palette. A clip is baked once into 60 Hz local poses; Advance
// interpolates between baked frames. Up to eight baked clips are kept, least recently used first
// out. Joints must be ordered so that a parent precedes its children; a parent index outside
// [0, joint) makes the joint a root. The bake cache keeps its set and skeleton alive until eviction.
class AnimationPlayer {
public:
    // Selects a clip and rewinds to time 0. A clip index past the set leaves HasClip() false.
    void Set(std::shared_ptr<const Domain::Skeleton> skeleton, std::shared_ptr<const Domain::AnimationSet> set,
             uint32_t clip);
    // Rest palette, time 0, not playing.
    void Stop();
    // Returns true when the palette changed.
    bool Advance(float dt);

    void SetTime(float time);
    float Time() const { return m_time; }
    float Duration() const;
    void SetPlaying(bool playing) { m_playing = playing; }
    bool IsPlaying() const { return m_playing; }
    void SetSpeed(float speed) { m_speed = speed; }
    void SetLoop(LoopMode loop) { m_loop = loop; }

    // One mat4 per skeleton joint: world * inverseBind.
    std::span<const glm::mat4> Palette() const { return m_palette; }
    // One mat4 per skeleton joint, for the skeleton overlay.
    std::span<const glm::mat4> WorldTransforms() const { return m_world; }

    bool HasClip() const { return m_baked != nullptr; }
    uint32_t Clip() const { return m_clip; }

    // Number of baked clips currently cached, and the total number of bakes performed.
    size_t BakedClips() const { return m_cache.size(); }
    size_t BakeCount() const { return m_bakeCount; }

private:
    struct Pose {
        glm::vec3 translation{0.0f};
        glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
        glm::vec3 scale{1.0f};
    };

    struct Baked {
        std::shared_ptr<const Domain::Skeleton> skeleton;
        std::shared_ptr<const Domain::AnimationSet> set;
        uint32_t clip = 0;
        float duration = 0.0f;
        size_t frameCount = 0;
        std::vector<Pose> poses;  // frameCount * joint count
    };

    std::shared_ptr<const Baked> FindOrBake();
    void Evaluate();
    void ApplyRest();

    std::shared_ptr<const Domain::Skeleton> m_skeleton;
    std::shared_ptr<const Domain::AnimationSet> m_set;
    std::list<std::shared_ptr<const Baked>> m_cache;  // most recently used first
    std::shared_ptr<const Baked> m_baked;
    std::vector<glm::mat4> m_restWorld;
    std::vector<glm::mat4> m_restPalette;
    std::vector<glm::mat4> m_world;
    std::vector<glm::mat4> m_palette;
    size_t m_bakeCount = 0;
    uint32_t m_clip = 0;
    float m_time = 0.0f;
    float m_speed = 1.0f;
    float m_direction = 1.0f;
    bool m_playing = false;
    LoopMode m_loop = LoopMode::Loop;
};

}
