#pragma once

#include <cstdint>

namespace Onyx::Domain {

/// Represents the high-level semantic "kind" of media an asset represents,
/// regardless of the underlying game version or specific format.
/// This abstraction allows the UI to be game-agnostic.
enum class MediaKind : uint8_t {
    Unknown,
    Image,    // any decoded 2D pixel surface, regardless of the source container's texture format
    Mesh,     // any parsed 3D model/geometry asset, regardless of the source container's mesh format
    Material, // Material definitions
    Skeleton, // Rig / Bones / Object instances
    Animation,// Animation clips
    Audio,    // any decoded audio clip, regardless of the source container's audio codec (e.g. VAG, SBK)
    Video,    // VPK, PSS, PSW
    Script,   // Game scripts
    Map,      // Contexts / Scenes
    Shader,   // GPU Shaders
    Container,// Wad, Pak, Iso (navigable archives)
    Raw       // Unknown bytes
};

/// Returns a human-readable name for a MediaKind.
const char* Name(MediaKind kind);

/// Returns an SF Symbol icon string for a MediaKind.
const char* Icon(MediaKind kind);

} // namespace Onyx::Domain
