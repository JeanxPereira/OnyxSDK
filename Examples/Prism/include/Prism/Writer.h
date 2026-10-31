#pragma once

#include <Prism/Format.h>

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Prism {

// Builds PRM1 containers. The palette section comes first; the other sections keep the order they
// were added in.
class Writer {
public:
    Writer& SetPalette(std::vector<glm::u8vec4> colors);
    Writer& AddImage(std::string name, uint16_t width, uint16_t height, std::vector<uint8_t> indices);
    Writer& AddManifest(std::string name, std::string_view text);
    Writer& AddClip(std::string name, uint32_t sampleRate, uint16_t channels, const std::vector<int16_t>& samples);
    // The column count is the width of the first row; shorter rows are padded with empty cells.
    Writer& AddEntries(std::string name, const std::vector<std::vector<std::string>>& rows);
    Writer& AddStats(std::string name, uint32_t imageCount, uint32_t clipCount, float gainDefault, uint32_t flags);

    // A section declaring an absolute range and storing no payload, to build out-of-bounds sections.
    Writer& AddRange(std::string name, Kind kind, uint32_t offset, uint32_t size);

    // Returns the container bytes, or an empty vector when the header, a section or the file would
    // exceed UINT32_MAX bytes, because every offset and size in the format is 32 bits.
    std::vector<std::byte> Build() const;

private:
    struct Item {
        std::string name;
        Kind kind = Kind::Image;
        bool declared = false;
        uint32_t offset = 0;
        uint32_t size = 0;
        std::vector<std::byte> payload;
    };

    Writer& Add(std::string name, Kind kind, std::vector<std::byte> payload);

    std::vector<glm::u8vec4> m_palette;
    bool m_hasPalette = false;
    std::vector<Item> m_items;
    bool m_overflow = false;
};

}
