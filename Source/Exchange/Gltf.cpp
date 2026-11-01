#define CGLTF_WRITE_IMPLEMENTATION
#include <cgltf_write.h>

#include <Onyx/Core/Services/Log.h>
#include <Onyx/Core/Services/Paths.h>
#include <Onyx/Exchange/Gltf.h>

#include "PngEncode.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_access.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace Onyx::Exchange {

namespace {

void AppendU32(std::string& out, uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<char>((value >> shift) & 0xFFu));
}

// Wraps a JSON document and a binary payload into a GLB container: header, JSON chunk padded with spaces, BIN chunk
// padded with zeros.
std::string PackGlb(const std::string& json, const std::vector<uint8_t>& bin) {
    const size_t jsonPad = (4 - json.size() % 4) % 4;
    const size_t binPad = (4 - bin.size() % 4) % 4;
    const size_t total = 12 + 8 + json.size() + jsonPad + (bin.empty() ? 0 : 8 + bin.size() + binPad);
    std::string glb;
    glb.reserve(total);
    AppendU32(glb, 0x46546C67u);
    AppendU32(glb, 2u);
    AppendU32(glb, static_cast<uint32_t>(total));
    AppendU32(glb, static_cast<uint32_t>(json.size() + jsonPad));
    AppendU32(glb, 0x4E4F534Au);
    glb += json;
    glb.append(jsonPad, ' ');
    if (!bin.empty()) {
        AppendU32(glb, static_cast<uint32_t>(bin.size() + binPad));
        AppendU32(glb, 0x004E4942u);
        glb.append(reinterpret_cast<const char*>(bin.data()), bin.size());
        glb.append(binPad, '\0');
    }
    return glb;
}

char* Mutable(const char* s) {
    return const_cast<char*>(s);
}

Result<void> Fail(const char* code, std::string message) {
    return Result<void>::Fail({code, std::move(message), 0});
}

// A relative URI reference for a UTF-8 file name: unreserved bytes stay, every other byte (space, '%', '#', '?',
// and each byte of a non-ASCII character) becomes %XX, which glTF loaders decode back to the name.
std::string EncodeUri(std::string_view name) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string uri;
    for (const char c : name) {
        const auto byte = static_cast<unsigned char>(c);
        const bool unreserved = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
                                (byte >= '0' && byte <= '9') || byte == '-' || byte == '.' || byte == '_' ||
                                byte == '~';
        if (unreserved) {
            uri.push_back(c);
        } else {
            uri.push_back('%');
            uri.push_back(kHex[byte >> 4]);
            uri.push_back(kHex[byte & 0x0Fu]);
        }
    }
    return uri;
}

// Writes `bytes` to `path` and reports whether every byte reached the file, the close included.
bool WriteFile(const std::filesystem::path& path, const char* bytes, size_t size) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (file) file.write(bytes, static_cast<std::streamsize>(size));
    file.close();
    return static_cast<bool>(file);
}

std::filesystem::path TemporaryBeside(const std::filesystem::path& target) {
    std::filesystem::path temporary = target;
    temporary += ".tmp";
    return temporary;
}

// One buffer; every blob starts on a 4-byte boundary so each view satisfies every attribute alignment rule.
struct Blob {
    std::vector<uint8_t> bytes;

    size_t Append(const void* data, size_t size) {
        bytes.resize((bytes.size() + 3) & ~size_t{3}, 0);
        const size_t offset = bytes.size();
        const auto* p = static_cast<const uint8_t*>(data);
        bytes.insert(bytes.end(), p, p + size);
        return offset;
    }
};

void MinMax(const std::vector<float>& flat, size_t count, int dim, float* mn, float* mx) {
    for (int k = 0; k < dim; ++k) mn[k] = mx[k] = count ? flat[static_cast<size_t>(k)] : 0.0f;
    for (size_t i = 1; i < count; ++i) {
        for (int k = 0; k < dim; ++k) {
            const float v = flat[i * static_cast<size_t>(dim) + static_cast<size_t>(k)];
            mn[k] = std::min(mn[k], v);
            mx[k] = std::max(mx[k], v);
        }
    }
}

// Splits a joint's local bind into translation, rotation and scale. A matrix whose axes are orthonormal keeps a
// scale of exactly one; a mirrored basis puts the reflection on the x scale.
struct Trs {
    glm::vec3 translation{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};
};

Trs Decompose(const glm::mat4& m) {
    Trs trs;
    trs.translation = glm::vec3(m[3]);
    glm::vec3 axes[3] = {glm::vec3(m[0]), glm::vec3(m[1]), glm::vec3(m[2])};
    for (int i = 0; i < 3; ++i) {
        const float length = glm::length(axes[i]);
        trs.scale[i] = std::abs(length - 1.0f) < 1e-5f ? 1.0f : length;
    }
    if (glm::dot(glm::cross(axes[0], axes[1]), axes[2]) < 0.0f) trs.scale.x = -trs.scale.x;
    glm::mat3 basis;
    for (int i = 0; i < 3; ++i) {
        basis[i] = trs.scale[i] != 0.0f ? axes[i] / trs.scale[i] : axes[i];
    }
    trs.rotation = glm::normalize(glm::quat_cast(basis));
    return trs;
}

// The arrays handed to cgltf must never reallocate after a pointer into them is taken (cgltf_write derives every
// index by pointer subtraction), so each is reserved for its upper bound before the first element.
template <class T>
std::vector<T> Reserved(size_t capacity) {
    std::vector<T> v;
    v.reserve(capacity);
    return v;
}

}

Result<void> WriteGltf(const Domain::Scene& scene, const std::filesystem::path& out, const GltfOptions& options) {
    std::vector<const Domain::MeshPart*> parts;
    for (const Domain::MeshPart& part : scene.parts) {
        if (!part.positions.empty()) parts.push_back(&part);
    }
    if (parts.empty()) return Fail("exchange.gltf.empty", "the scene has no mesh part with vertices");

    const Domain::Skeleton* skeleton = scene.skeleton && !scene.skeleton->joints.empty() ? scene.skeleton.get() : nullptr;
    const size_t jointCount = skeleton ? skeleton->joints.size() : 0;
    const bool hasSkin = skeleton && options.includeSkin;

    std::vector<const Domain::AnimClip*> clips;
    if (skeleton && options.includeAnimations && scene.animations) {
        for (const Domain::AnimClip& clip : scene.animations->clips) {
            if (!clip.additive) clips.push_back(&clip);
        }
    }

    // Textures that materials reference through an exported role and that can be embedded.
    std::vector<uint32_t> usedTextures;
    std::map<uint32_t, size_t> textureSlot;
    std::vector<std::vector<std::byte>> encoded;
    for (const Domain::Material& material : scene.materials) {
        for (const Domain::TextureRole role :
             {Domain::TextureRole::Diffuse, Domain::TextureRole::Normal, Domain::TextureRole::Occlusion}) {
            const auto it = material.textures.find(role);
            if (it == material.textures.end() || it->second >= scene.textures.size() || textureSlot.count(it->second)) {
                continue;
            }
            std::vector<std::byte> png = EncodePng(scene.textures[it->second]);
            if (png.empty()) continue;
            textureSlot[it->second] = usedTextures.size();
            usedTextures.push_back(it->second);
            encoded.push_back(std::move(png));
        }
    }
    const size_t imageCount = usedTextures.size();

    size_t accessorBound = parts.size() * 9 + 1;
    for (const Domain::AnimClip* clip : clips) accessorBound += clip->channels.size() * 6;

    auto accessors = Reserved<cgltf_accessor>(accessorBound);
    auto views = Reserved<cgltf_buffer_view>(accessorBound + imageCount);
    std::vector<cgltf_buffer> buffers(1);
    std::vector<cgltf_material> materials(scene.materials.size());
    std::vector<cgltf_image> images(imageCount);
    std::vector<cgltf_texture> textures(imageCount);
    std::vector<cgltf_sampler> samplers(imageCount ? 1 : 0);
    std::vector<cgltf_node> nodes(1 + jointCount);
    std::vector<cgltf_mesh> meshes(1);
    std::vector<cgltf_skin> skins(hasSkin ? 1 : 0);
    std::vector<cgltf_scene> scenes(1);
    std::vector<cgltf_primitive> primitives(parts.size());
    std::vector<std::vector<cgltf_attribute>> attributes(parts.size());
    std::vector<std::vector<cgltf_node*>> children(jointCount);
    auto animations = Reserved<cgltf_animation>(clips.size());
    auto animSamplers = Reserved<std::vector<cgltf_animation_sampler>>(clips.size());
    auto animChannels = Reserved<std::vector<cgltf_animation_channel>>(clips.size());

    Blob blob;

    auto addView = [&](size_t offset, size_t size) -> cgltf_buffer_view* {
        cgltf_buffer_view& view = views.emplace_back();
        view = cgltf_buffer_view{};
        view.buffer = &buffers[0];
        view.offset = offset;
        view.size = size;
        return &view;
    };

    auto addAccessor = [&](cgltf_type type, cgltf_component_type component, size_t count, const void* data,
                           size_t elementSize, const std::vector<float>& flat, int dim,
                           bool bounded) -> cgltf_accessor* {
        const size_t offset = blob.Append(data, elementSize * count);
        cgltf_accessor& accessor = accessors.emplace_back();
        accessor = cgltf_accessor{};
        accessor.type = type;
        accessor.component_type = component;
        accessor.count = count;
        accessor.buffer_view = addView(offset, elementSize * count);
        if (bounded) {
            accessor.has_min = accessor.has_max = true;
            MinMax(flat, count, dim, accessor.min, accessor.max);
        }
        return &accessor;
    };

    auto addFloats = [&](cgltf_type type, int dim, const std::vector<float>& flat) {
        const size_t count = flat.size() / static_cast<size_t>(dim);
        return addAccessor(type, cgltf_component_type_r_32f, count, flat.data(), sizeof(float) * static_cast<size_t>(dim),
                           flat, dim, true);
    };

    auto flatten2 = [](const std::vector<glm::vec2>& v) {
        return std::vector<float>(glm::value_ptr(v[0]), glm::value_ptr(v[0]) + v.size() * 2);
    };
    auto flatten3 = [](const std::vector<glm::vec3>& v) {
        return std::vector<float>(glm::value_ptr(v[0]), glm::value_ptr(v[0]) + v.size() * 3);
    };
    auto flatten4 = [](const std::vector<glm::vec4>& v) {
        return std::vector<float>(glm::value_ptr(v[0]), glm::value_ptr(v[0]) + v.size() * 4);
    };

    for (size_t pi = 0; pi < parts.size(); ++pi) {
        const Domain::MeshPart& part = *parts[pi];
        const size_t n = part.positions.size();
        std::vector<cgltf_attribute>& attrs = attributes[pi];
        attrs.reserve(8);
        auto addAttr = [&](const char* name, cgltf_attribute_type type, int index, cgltf_accessor* accessor) {
            attrs.push_back(cgltf_attribute{Mutable(name), type, index, accessor});
        };

        addAttr("POSITION", cgltf_attribute_type_position, 0, addFloats(cgltf_type_vec3, 3, flatten3(part.positions)));
        if (part.normals.size() == n) {
            addAttr("NORMAL", cgltf_attribute_type_normal, 0, addFloats(cgltf_type_vec3, 3, flatten3(part.normals)));
        }
        if (part.tangents.size() == n) {
            addAttr("TANGENT", cgltf_attribute_type_tangent, 0, addFloats(cgltf_type_vec4, 4, flatten4(part.tangents)));
        }
        if (part.uv0.size() == n) {
            addAttr("TEXCOORD_0", cgltf_attribute_type_texcoord, 0, addFloats(cgltf_type_vec2, 2, flatten2(part.uv0)));
        }
        if (part.uv1.size() == n) {
            addAttr("TEXCOORD_1", cgltf_attribute_type_texcoord, 1, addFloats(cgltf_type_vec2, 2, flatten2(part.uv1)));
        }
        if (part.colors.size() == n) {
            addAttr("COLOR_0", cgltf_attribute_type_color, 0, addFloats(cgltf_type_vec4, 4, flatten4(part.colors)));
        }
        if (hasSkin && !part.jointMap.empty() && part.joints.size() == n && part.weights.size() == n) {
            // An unmapped local index, or one mapped past the skeleton, becomes joint 0 with weight 0.
            std::vector<uint16_t> joints(n * 4);
            std::vector<float> jointsFlat(n * 4);
            std::vector<float> weights = flatten4(part.weights);
            for (size_t v = 0; v < n; ++v) {
                for (size_t c = 0; c < 4; ++c) {
                    const size_t local = part.joints[v][static_cast<glm::length_t>(c)];
                    uint16_t global = 0;
                    if (local < part.jointMap.size() && part.jointMap[local] < jointCount) {
                        global = part.jointMap[local];
                    } else {
                        weights[v * 4 + c] = 0.0f;
                    }
                    joints[v * 4 + c] = global;
                    jointsFlat[v * 4 + c] = static_cast<float>(global);
                }
            }
            addAttr("JOINTS_0", cgltf_attribute_type_joints, 0,
                    addAccessor(cgltf_type_vec4, cgltf_component_type_r_16u, n, joints.data(), sizeof(uint16_t) * 4,
                                jointsFlat, 4, false));
            addAttr("WEIGHTS_0", cgltf_attribute_type_weights, 0, addFloats(cgltf_type_vec4, 4, weights));
        }

        std::vector<float> indexFlat(part.indices.begin(), part.indices.end());
        cgltf_accessor* indexAccessor = nullptr;
        if (!part.indices.empty()) {
            indexAccessor = addAccessor(cgltf_type_scalar, cgltf_component_type_r_32u, part.indices.size(),
                                        part.indices.data(), sizeof(uint32_t), indexFlat, 1, false);
        }

        cgltf_primitive& primitive = primitives[pi];
        primitive.type = cgltf_primitive_type_triangles;
        primitive.indices = indexAccessor;
        primitive.material = part.materialIndex < materials.size() ? &materials[part.materialIndex] : nullptr;
        primitive.attributes = attrs.data();
        primitive.attributes_count = attrs.size();
    }

    cgltf_accessor* inverseBind = nullptr;
    if (hasSkin) {
        std::vector<float> flat(jointCount * 16);
        for (size_t j = 0; j < jointCount; ++j) {
            std::memcpy(&flat[j * 16], glm::value_ptr(skeleton->joints[j].inverseBind), sizeof(float) * 16);
        }
        inverseBind = addFloats(cgltf_type_mat4, 16, flat);
    }

    if (imageCount) {
        samplers[0].mag_filter = cgltf_filter_type_linear;
        samplers[0].min_filter = cgltf_filter_type_linear;
        samplers[0].wrap_s = cgltf_wrap_mode_repeat;
        samplers[0].wrap_t = cgltf_wrap_mode_repeat;
    }
    for (size_t k = 0; k < imageCount; ++k) {
        const size_t offset = blob.Append(encoded[k].data(), encoded[k].size());
        images[k].buffer_view = addView(offset, encoded[k].size());
        images[k].mime_type = Mutable("image/png");
        textures[k].image = &images[k];
        textures[k].sampler = &samplers[0];
    }

    for (size_t mi = 0; mi < materials.size(); ++mi) {
        const Domain::Material& src = scene.materials[mi];
        cgltf_material& m = materials[mi];
        m.name = Mutable(src.name.c_str());
        m.has_pbr_metallic_roughness = true;
        for (int c = 0; c < 4; ++c) m.pbr_metallic_roughness.base_color_factor[c] = src.baseColor[c];
        m.pbr_metallic_roughness.metallic_factor = src.metallic;
        m.pbr_metallic_roughness.roughness_factor = 1.0f;
        m.alpha_mode = (src.blend != Domain::BlendMode::Opaque || src.baseColor.a < 0.999f) ? cgltf_alpha_mode_blend
                                                                                              : cgltf_alpha_mode_opaque;
        m.double_sided = true;

        auto bind = [&](Domain::TextureRole role, cgltf_texture_view& view) {
            const auto it = src.textures.find(role);
            if (it == src.textures.end()) return;
            const auto slot = textureSlot.find(it->second);
            if (slot == textureSlot.end()) return;
            view.texture = &textures[slot->second];
            view.texcoord = 0;
            view.scale = 1.0f;
        };
        bind(Domain::TextureRole::Diffuse, m.pbr_metallic_roughness.base_color_texture);
        bind(Domain::TextureRole::Normal, m.normal_texture);
        bind(Domain::TextureRole::Occlusion, m.occlusion_texture);
    }

    for (size_t j = 0; j < jointCount; ++j) {
        const Domain::Joint& joint = skeleton->joints[j];
        cgltf_node& node = nodes[1 + j];
        const Trs trs = Decompose(joint.localBind);
        node.name = Mutable(joint.name.c_str());
        node.has_translation = node.has_rotation = node.has_scale = true;
        for (int c = 0; c < 3; ++c) {
            node.translation[c] = trs.translation[c];
            node.scale[c] = trs.scale[c];
        }
        node.rotation[0] = trs.rotation.x;
        node.rotation[1] = trs.rotation.y;
        node.rotation[2] = trs.rotation.z;
        node.rotation[3] = trs.rotation.w;
    }
    for (size_t j = 0; j < jointCount; ++j) {
        const int32_t parent = skeleton->joints[j].parent;
        if (parent >= 0 && static_cast<size_t>(parent) < jointCount && static_cast<size_t>(parent) != j) {
            children[static_cast<size_t>(parent)].push_back(&nodes[1 + j]);
        }
    }
    for (size_t j = 0; j < jointCount; ++j) {
        if (children[j].empty()) continue;
        nodes[1 + j].children = children[j].data();
        nodes[1 + j].children_count = children[j].size();
    }

    auto isRoot = [&](size_t j) {
        const int32_t parent = skeleton->joints[j].parent;
        return parent < 0 || static_cast<size_t>(parent) >= jointCount || static_cast<size_t>(parent) == j;
    };

    cgltf_node& meshNode = nodes[0];
    meshNode.name = Mutable("Mesh");
    meshNode.mesh = &meshes[0];
    meshNode.has_matrix = true;
    std::memcpy(meshNode.matrix, glm::value_ptr(scene.transform), sizeof(float) * 16);

    std::vector<cgltf_node*> jointNodes(hasSkin ? jointCount : 0);
    if (hasSkin) {
        size_t rootCount = 0;
        size_t rootIndex = 0;
        for (size_t j = 0; j < jointCount; ++j) {
            jointNodes[j] = &nodes[1 + j];
            if (isRoot(j)) {
                if (rootCount++ == 0) rootIndex = j;
            }
        }
        skins[0].joints = jointNodes.data();
        skins[0].joints_count = jointNodes.size();
        skins[0].inverse_bind_matrices = inverseBind;
        if (rootCount == 1) skins[0].skeleton = &nodes[1 + rootIndex];
        meshNode.skin = &skins[0];
    }

    meshes[0].name = Mutable("Mesh");
    meshes[0].primitives = primitives.data();
    meshes[0].primitives_count = primitives.size();

    // Animations: one sampler and one channel per populated key list. glTF forbids two channels on the same node and
    // path, so a later key list for a (joint, path) already written is skipped with a warning. Rotation keys are
    // normalised, and a zero quaternion becomes the identity. A clip without a usable channel is dropped.
    for (const Domain::AnimClip* clip : clips) {
        std::vector<cgltf_animation_sampler> samplerList;
        std::vector<size_t> channelJoint;
        std::vector<cgltf_animation_path_type> channelPath;

        auto taken = [&](size_t joint, cgltf_animation_path_type path) {
            for (size_t k = 0; k < channelJoint.size(); ++k) {
                if (channelJoint[k] == joint && channelPath[k] == path) {
                    Services::Log::Default().Write(Services::LogLevel::Warn, "exchange.gltf",
                                                   "clip {}: joint {} has a second {} key list; it is skipped",
                                                   clip->name, joint,
                                                   path == cgltf_animation_path_type_rotation      ? "rotation"
                                                   : path == cgltf_animation_path_type_translation ? "translation"
                                                                                                   : "scale");
                    return true;
                }
            }
            return false;
        };

        auto emit = [&](const std::vector<float>& times, cgltf_accessor* output, size_t joint,
                        cgltf_animation_path_type path) {
            cgltf_animation_sampler sampler{};
            sampler.input = addAccessor(cgltf_type_scalar, cgltf_component_type_r_32f, times.size(), times.data(),
                                        sizeof(float), times, 1, true);
            sampler.output = output;
            sampler.interpolation = cgltf_interpolation_type_linear;
            samplerList.push_back(sampler);
            channelJoint.push_back(joint);
            channelPath.push_back(path);
        };

        for (const Domain::AnimChannel& channel : clip->channels) {
            if (channel.joint >= jointCount) continue;
            if (!channel.rotation.empty() && !taken(channel.joint, cgltf_animation_path_type_rotation)) {
                std::vector<float> times, values;
                for (const auto& key : channel.rotation) {
                    times.push_back(key.time);
                    const float length = glm::length(key.value);
                    const glm::quat q = length > 0.0f && std::isfinite(length) ? key.value / length
                                                                                : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                    values.insert(values.end(), {q.x, q.y, q.z, q.w});
                }
                emit(times, addFloats(cgltf_type_vec4, 4, values), channel.joint, cgltf_animation_path_type_rotation);
            }
            if (!channel.translation.empty() && !taken(channel.joint, cgltf_animation_path_type_translation)) {
                std::vector<float> times, values;
                for (const auto& key : channel.translation) {
                    times.push_back(key.time);
                    values.insert(values.end(), {key.value.x, key.value.y, key.value.z});
                }
                emit(times, addFloats(cgltf_type_vec3, 3, values), channel.joint, cgltf_animation_path_type_translation);
            }
            if (!channel.scale.empty() && !taken(channel.joint, cgltf_animation_path_type_scale)) {
                std::vector<float> times, values;
                for (const auto& key : channel.scale) {
                    times.push_back(key.time);
                    values.insert(values.end(), {key.value.x, key.value.y, key.value.z});
                }
                emit(times, addFloats(cgltf_type_vec3, 3, values), channel.joint, cgltf_animation_path_type_scale);
            }
        }
        if (samplerList.empty()) continue;

        std::vector<cgltf_animation_sampler>& keptSamplers = animSamplers.emplace_back(std::move(samplerList));
        std::vector<cgltf_animation_channel>& keptChannels = animChannels.emplace_back(keptSamplers.size());
        for (size_t k = 0; k < keptChannels.size(); ++k) {
            keptChannels[k].sampler = &keptSamplers[k];
            keptChannels[k].target_node = &nodes[1 + channelJoint[k]];
            keptChannels[k].target_path = channelPath[k];
        }
        cgltf_animation& animation = animations.emplace_back();
        animation = cgltf_animation{};
        animation.name = Mutable(clip->name.c_str());
        animation.samplers = keptSamplers.data();
        animation.samplers_count = keptSamplers.size();
        animation.channels = keptChannels.data();
        animation.channels_count = keptChannels.size();
    }

    std::vector<cgltf_node*> sceneNodes{&meshNode};
    for (size_t j = 0; j < jointCount; ++j) {
        if (isRoot(j)) sceneNodes.push_back(&nodes[1 + j]);
    }
    scenes[0].nodes = sceneNodes.data();
    scenes[0].nodes_count = sceneNodes.size();

    buffers[0].size = blob.bytes.size();

    cgltf_data data{};
    data.asset.version = Mutable("2.0");
    data.asset.generator = Mutable("Onyx Exchange");
    data.meshes = meshes.data();
    data.meshes_count = meshes.size();
    data.materials = materials.empty() ? nullptr : materials.data();
    data.materials_count = materials.size();
    data.accessors = accessors.data();
    data.accessors_count = accessors.size();
    data.buffer_views = views.data();
    data.buffer_views_count = views.size();
    data.buffers = buffers.data();
    data.buffers_count = buffers.size();
    data.images = images.empty() ? nullptr : images.data();
    data.images_count = images.size();
    data.textures = textures.empty() ? nullptr : textures.data();
    data.textures_count = textures.size();
    data.samplers = samplers.empty() ? nullptr : samplers.data();
    data.samplers_count = samplers.size();
    data.skins = skins.empty() ? nullptr : skins.data();
    data.skins_count = skins.size();
    data.animations = animations.empty() ? nullptr : animations.data();
    data.animations_count = animations.size();
    data.nodes = nodes.data();
    data.nodes_count = nodes.size();
    data.scenes = scenes.data();
    data.scenes_count = scenes.size();
    data.scene = &scenes[0];

    std::error_code ec;
    if (out.has_parent_path()) std::filesystem::create_directories(out.parent_path(), ec);

    cgltf_options writeOptions{};
    std::filesystem::path binPath;
    std::string binUri;
    if (options.embedBuffers) {
        writeOptions.type = cgltf_file_type_glb;
        data.file_type = cgltf_file_type_glb;
        data.bin = blob.bytes.data();
        data.bin_size = blob.bytes.size();
    } else {
        writeOptions.type = cgltf_file_type_gltf;
        data.file_type = cgltf_file_type_gltf;
        binPath = out;
        binPath.replace_extension(".bin");
        if (binPath == out) {
            return Fail("exchange.gltf.write", Services::Paths::ToUtf8(out) + " would also be the name of its .bin buffer");
        }
        binUri = EncodeUri(Services::Paths::ToUtf8(binPath.filename()));
        buffers[0].uri = Mutable(binUri.c_str());
    }

    const cgltf_size needed = cgltf_write(&writeOptions, nullptr, 0, &data);
    if (needed == 0) return Fail("exchange.gltf.write", "cannot encode " + Services::Paths::ToUtf8(out));
    std::string document(needed, '\0');
    cgltf_write(&writeOptions, document.data(), document.size(), &data);
    // cgltf_write emits JSON only and counts a terminating NUL, which neither form keeps.
    if (!document.empty() && document.back() == '\0') document.pop_back();
    if (options.embedBuffers) document = PackGlb(document, blob.bytes);

    // Both files land under temporary names first; an existing pair is replaced only once both are complete.
    const std::filesystem::path documentTemporary = TemporaryBeside(out);
    const std::filesystem::path binTemporary = binPath.empty() ? std::filesystem::path() : TemporaryBeside(binPath);
    const auto discard = [&] {
        std::filesystem::remove(documentTemporary, ec);
        if (!binTemporary.empty()) std::filesystem::remove(binTemporary, ec);
    };

    if (!binTemporary.empty() &&
        !WriteFile(binTemporary, reinterpret_cast<const char*>(blob.bytes.data()), blob.bytes.size())) {
        discard();
        return Fail("exchange.gltf.write", "cannot write " + Services::Paths::ToUtf8(binPath));
    }
    if (!WriteFile(documentTemporary, document.data(), document.size())) {
        discard();
        return Fail("exchange.gltf.write", "cannot write " + Services::Paths::ToUtf8(out));
    }
    std::filesystem::rename(documentTemporary, out, ec);
    if (ec) {
        discard();
        return Fail("exchange.gltf.write", "cannot replace " + Services::Paths::ToUtf8(out) + ": " + ec.message());
    }
    if (!binTemporary.empty()) {
        std::filesystem::rename(binTemporary, binPath, ec);
        if (ec) {
            discard();
            return Fail("exchange.gltf.write", "cannot replace " + Services::Paths::ToUtf8(binPath) + ": " + ec.message());
        }
    }
    return Result<void>::Ok();
}

}
