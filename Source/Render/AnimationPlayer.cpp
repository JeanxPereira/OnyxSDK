#include "Onyx/Render/AnimationPlayer.h"

#include "Onyx/Render/Skinning.h"

#include <algorithm>
#include <cmath>

namespace Onyx::Render {

namespace {

constexpr float kBakeRate = 60.0f;
constexpr size_t kMaxBakedClips = 8;

template <class T, class Lerp>
bool SampleKeys(const std::vector<Domain::Key<T>>& keys, float time, Lerp lerp, T& out) {
    if (keys.empty()) {
        return false;
    }
    const auto next = std::upper_bound(keys.begin(), keys.end(), time,
                                       [](float t, const Domain::Key<T>& key) { return t < key.time; });
    if (next == keys.begin()) {
        out = keys.front().value;
    } else if (next == keys.end()) {
        out = keys.back().value;
    } else {
        const Domain::Key<T>& a = *(next - 1);
        const Domain::Key<T>& b = *next;
        const float span = b.time - a.time;
        const float x = span > 0.0f ? (time - a.time) / span : 0.0f;
        out = lerp(a.value, b.value, x);
    }
    return true;
}

glm::vec3 LerpVec(const glm::vec3& a, const glm::vec3& b, float x) {
    return glm::mix(a, b, x);
}

glm::quat SlerpQuat(const glm::quat& a, const glm::quat& b, float x) {
    return glm::slerp(a, b, x);
}

glm::mat4 Compose(const glm::vec3& translation, const glm::quat& rotation, const glm::vec3& scale) {
    glm::mat4 m = glm::mat4_cast(rotation);
    m[0] *= scale.x;
    m[1] *= scale.y;
    m[2] *= scale.z;
    m[3] = glm::vec4(translation, 1.0f);
    return m;
}

}

void AnimationPlayer::Set(std::shared_ptr<const Domain::Skeleton> skeleton,
                          std::shared_ptr<const Domain::AnimationSet> set, uint32_t clip) {
    m_skeleton = std::move(skeleton);
    m_set = std::move(set);
    m_clip = clip;
    m_time = 0.0f;
    m_direction = 1.0f;

    if (m_skeleton) {
        m_restWorld = RestWorldTransforms(*m_skeleton);
        m_restPalette = RestPalette(*m_skeleton);
        m_scratch.resize(m_skeleton->joints.size());
        m_world.resize(m_skeleton->joints.size());
        m_palette.resize(m_skeleton->joints.size());
    } else {
        m_restWorld.clear();
        m_restPalette.clear();
        m_scratch.clear();
        m_world.clear();
        m_palette.clear();
    }

    m_baked = FindOrBake();
    if (m_baked) {
        Evaluate();
    } else {
        ApplyRest();
    }
}

void AnimationPlayer::Stop() {
    m_playing = false;
    m_time = 0.0f;
    m_direction = 1.0f;
    ApplyRest();
}

float AnimationPlayer::Duration() const {
    return m_baked ? m_baked->duration : 0.0f;
}

void AnimationPlayer::SetTime(float time) {
    m_direction = 1.0f;
    m_time = std::clamp(time, 0.0f, Duration());
    if (m_baked) {
        Evaluate();
    }
}

bool AnimationPlayer::Advance(float dt) {
    const float duration = Duration();
    if (!m_playing || !m_baked || duration <= 0.0f) {
        return false;
    }

    const float step = dt * m_speed;
    float time = m_time;
    switch (m_loop) {
        case LoopMode::None:
            time = std::clamp(m_time + step, 0.0f, duration);
            if ((step > 0.0f && time >= duration) || (step < 0.0f && time <= 0.0f)) {
                m_playing = false;
            }
            break;
        case LoopMode::Loop:
            time = std::fmod(m_time + step, duration);
            if (time < 0.0f) {
                time += duration;
            }
            break;
        case LoopMode::PingPong: {
            const float period = 2.0f * duration;
            float phase = (m_direction > 0.0f ? m_time : period - m_time) + step;
            phase = std::fmod(phase, period);
            if (phase < 0.0f) {
                phase += period;
            }
            time = phase <= duration ? phase : period - phase;
            m_direction = phase < duration ? 1.0f : -1.0f;
            break;
        }
    }

    const bool changed = time != m_time;
    m_time = time;
    if (changed) {
        Evaluate();
    }
    return changed;
}

std::shared_ptr<const AnimationPlayer::Baked> AnimationPlayer::FindOrBake() {
    if (!m_set || !m_skeleton || m_clip >= m_set->clips.size()) {
        return nullptr;
    }

    for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
        const Baked& entry = **it;
        if (entry.set == m_set && entry.skeleton == m_skeleton && entry.clip == m_clip) {
            m_cache.splice(m_cache.begin(), m_cache, it);
            return m_cache.front();
        }
    }

    const Domain::AnimClip& clip = m_set->clips[m_clip];
    const size_t jointCount = m_skeleton->joints.size();

    auto baked = std::make_shared<Baked>();
    baked->skeleton = m_skeleton;
    baked->set = m_set;
    baked->clip = m_clip;
    baked->duration = std::max(clip.duration, 0.0f);
    baked->frameCount = static_cast<size_t>(std::ceil(baked->duration * kBakeRate)) + 1;
    baked->poses.resize(baked->frameCount * jointCount);

    std::vector<Pose> bind(jointCount);
    for (size_t j = 0; j < jointCount; ++j) {
        const glm::mat4& local = m_skeleton->joints[j].localBind;
        Pose& pose = bind[j];
        pose.translation = glm::vec3(local[3]);
        pose.scale = glm::vec3(glm::length(glm::vec3(local[0])), glm::length(glm::vec3(local[1])),
                               glm::length(glm::vec3(local[2])));
        const glm::vec3 inv = 1.0f / glm::max(pose.scale, glm::vec3(1e-12f));
        pose.rotation = glm::normalize(glm::quat_cast(
            glm::mat3(glm::vec3(local[0]) * inv.x, glm::vec3(local[1]) * inv.y, glm::vec3(local[2]) * inv.z)));
    }
    for (size_t f = 0; f < baked->frameCount; ++f) {
        std::copy(bind.begin(), bind.end(), baked->poses.begin() + static_cast<std::ptrdiff_t>(f * jointCount));
    }

    for (const Domain::AnimChannel& channel : clip.channels) {
        if (channel.joint >= jointCount) {
            continue;
        }
        const Pose& rest = bind[channel.joint];
        for (size_t f = 0; f < baked->frameCount; ++f) {
            const float time = static_cast<float>(f) / kBakeRate;
            Pose& pose = baked->poses[f * jointCount + channel.joint];
            glm::vec3 t;
            glm::quat r;
            glm::vec3 s;
            if (SampleKeys(channel.translation, time, LerpVec, t)) {
                pose.translation = clip.additive ? rest.translation + t : t;
            }
            if (SampleKeys(channel.rotation, time, SlerpQuat, r)) {
                pose.rotation = clip.additive ? glm::normalize(rest.rotation * r) : r;
            }
            if (SampleKeys(channel.scale, time, LerpVec, s)) {
                pose.scale = clip.additive ? rest.scale * s : s;
            }
        }
    }

    ++m_bakeCount;
    m_cache.push_front(baked);
    if (m_cache.size() > kMaxBakedClips) {
        m_cache.pop_back();
    }
    return baked;
}

void AnimationPlayer::Evaluate() {
    const size_t jointCount = m_skeleton->joints.size();
    const Baked& baked = *m_baked;

    const float frame = m_time * kBakeRate;
    const size_t last = baked.frameCount - 1;
    const size_t i0 = std::min(static_cast<size_t>(std::max(frame, 0.0f)), last);
    const size_t i1 = std::min(i0 + 1, last);
    const float x = i0 == last ? 0.0f : std::clamp(frame - static_cast<float>(i0), 0.0f, 1.0f);

    const Pose* a = baked.poses.data() + i0 * jointCount;
    const Pose* b = baked.poses.data() + i1 * jointCount;
    for (size_t j = 0; j < jointCount; ++j) {
        const Domain::Joint& joint = m_skeleton->joints[j];
        const glm::mat4 local = x == 0.0f
            ? Compose(a[j].translation, a[j].rotation, a[j].scale)
            : Compose(glm::mix(a[j].translation, b[j].translation, x), glm::slerp(a[j].rotation, b[j].rotation, x),
                      glm::mix(a[j].scale, b[j].scale, x));
        const bool hasParent = joint.parent >= 0 && static_cast<size_t>(joint.parent) < j;
        m_world[j] = hasParent ? m_world[static_cast<size_t>(joint.parent)] * local : local;
        m_palette[j] = m_world[j] * joint.inverseBind;
    }
}

void AnimationPlayer::ApplyRest() {
    m_world = m_restWorld;
    m_palette = m_restPalette;
}

}
