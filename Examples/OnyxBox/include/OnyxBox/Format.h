#pragma once

// OnyxBox is a synthetic container pair that exercises the module contracts without a real game
// format. All integers and floats are little-endian.
//
// OBX1 container
//   "OBX1" | u32 count | count x TOC record
//   TOC record: u16 nameLen | name (UTF-8) | u8 kind | u32 payloadOffset | u32 payloadSize
//   payloadOffset is absolute from the start of the file.
//
//   kind 0 blob   opaque bytes
//   kind 1 image  u16 width | u16 height | width * height * 4 bytes of RGBA8
//   kind 2 text   UTF-8 bytes, no terminator
//   kind 3 mesh   exactly 12 f32: baseColor RGBA | position xyz | half-extents xyz | 2 unused.
//                 Decodes to an axis-aligned box of 24 vertices and 12 triangles.
//   kind 4 table  u16 cols | u16 rows | rows x cols cells, each u16 len | UTF-8 bytes.
//                 Columns are named col0, col1, ...
//
// The TOC walk is defensive. A record whose payload range leaves the file, or a mesh whose payload
// is not 48 bytes, stays in the tree flagged Failed with an Error diagnostic. A record that runs
// past the end of the file stops the walk and keeps what was read.
//
// OBP1 pak (mounted for the extension "obxpak")
//   "OBP1" | u32 count | count x file record
//   file record: u32 nameLen | name (UTF-8, 1..255 bytes) | u32 size | size bytes of an OBX1 container
//
// A mounted pak parses every inner container into a subtree named after the inner file. Each
// inner file takes its own slot in the document's file table.

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace OnyxBox {

inline constexpr std::string_view kBoxMagic = "OBX1";
inline constexpr std::string_view kPakMagic = "OBP1";

enum class Kind : uint8_t {
    Blob = 0,
    Image = 1,
    Text = 2,
    Mesh = 3,
    Table = 4,
};

inline constexpr uint32_t kMeshPayloadSize = 12 * 4;
inline constexpr size_t kMinTocRecord = 2 + 1 + 4 + 4;
inline constexpr size_t kMinPakRecord = 4 + 1 + 4;
inline constexpr uint32_t kMaxPakNameLength = 255;

}
