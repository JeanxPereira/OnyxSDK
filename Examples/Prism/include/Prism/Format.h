#pragma once

// Prism is a synthetic indexed-colour container holding a palette shared through the document, a
// schema entry, a table that is also an any-capability payload, and settings that change how
// entries decode. All integers and floats are little-endian.
//
// PRM1 container
//   "PRM1" | u16 paletteCount | u32 sectionCount | sectionCount x section header
//   section header: u8 kind | u16 nameLen | name (UTF-8) | u32 offset | u32 size
//   offset is absolute from the start of the file.
//
//   kind 0 palette   paletteCount x RGBA8. Exactly one per file; parsed when the container opens
//                    because the indexed images need it to decode.
//   kind 1 image     u16 width | u16 height | width * height bytes of palette indices
//   kind 2 manifest  UTF-8 text (language "toml")
//   kind 3 clip      u32 sampleRate | u16 channels | u32 frameCount | frameCount * channels x i16 PCM
//   kind 4 entries   u16 cols | u16 rows | rows x cols cells, each u16 len | UTF-8 bytes.
//                    Columns are named by the first row.
//   kind 5 stats     u32 imageCount | u32 clipCount | f32 gainDefault | u32 flags
//
// The walk is defensive. A section whose range leaves the file, an image whose size is not
// 4 + width * height, a clip whose size does not match its frame count, a palette whose size does
// not match paletteCount, a second palette or a stats record that is not 16 bytes stays in the tree
// flagged Failed with an Error diagnostic. Without a palette every image is flagged Failed with
// "prism.noPalette". A header that runs past the end of the file stops the walk and keeps what was
// read. An index past paletteCount is found when the image decodes.

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Prism {

inline constexpr std::string_view kMagic = "PRM1";
inline constexpr std::string_view kTransparentIndexZeroKey = "prism.transparentIndexZero";
inline constexpr std::string_view kGainKey = "prism.gain";

enum class Kind : uint8_t {
    Palette = 0,
    Image = 1,
    Manifest = 2,
    Clip = 3,
    Entries = 4,
    Stats = 5,
};

inline constexpr size_t kFileHeaderSize = 4 + 2 + 4;
inline constexpr size_t kMinSectionHeader = 1 + 2 + 4 + 4;
inline constexpr size_t kImageHeaderSize = 4;
inline constexpr size_t kClipHeaderSize = 4 + 2 + 4;
inline constexpr uint32_t kStatsSize = 16;

}
