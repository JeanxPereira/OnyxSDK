#pragma once

#include <OnyxBox/Format.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace OnyxBox {

// Builds OBX1 containers and OBP1 paks. Entries keep the order they were added in.
class Writer {
public:
    Writer& AddBlob(std::string name, std::vector<std::byte> bytes);
    Writer& AddImage(std::string name, uint16_t width, uint16_t height, const std::vector<std::byte>& rgba);
    Writer& AddText(std::string name, std::string_view text);
    Writer& AddMesh(std::string name, std::array<float, 4> baseColor, std::array<float, 3> position,
                    std::array<float, 3> halfExtents);
    // The column count is the width of the first row; shorter rows are padded with empty cells.
    Writer& AddTable(std::string name, const std::vector<std::vector<std::string>>& rows);

    // A payload stored verbatim, whatever its kind expects.
    Writer& AddEntry(std::string name, Kind kind, std::vector<std::byte> payload);
    // A record declaring an absolute range and storing no payload, to build out-of-bounds records.
    Writer& AddRange(std::string name, Kind kind, uint32_t offset, uint32_t size);

    std::vector<std::byte> Build() const;

    static std::vector<std::byte> BuildPak(const std::vector<std::pair<std::string, std::vector<std::byte>>>& files);

private:
    struct Item {
        std::string name;
        Kind kind = Kind::Blob;
        bool declared = false;
        uint32_t offset = 0;
        uint32_t size = 0;
        std::vector<std::byte> payload;
    };

    std::vector<Item> m_items;
};

}
