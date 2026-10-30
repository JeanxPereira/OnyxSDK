#include <OnyxBox/Writer.h>

#include <algorithm>
#include <cstring>

namespace OnyxBox {

namespace {

void PutU8(std::vector<std::byte>& out, uint8_t v) {
    out.push_back(static_cast<std::byte>(v));
}

void PutU16(std::vector<std::byte>& out, uint16_t v) {
    PutU8(out, static_cast<uint8_t>(v & 0xFF));
    PutU8(out, static_cast<uint8_t>(v >> 8));
}

void PutU32(std::vector<std::byte>& out, uint32_t v) {
    PutU16(out, static_cast<uint16_t>(v & 0xFFFF));
    PutU16(out, static_cast<uint16_t>(v >> 16));
}

void PutF32(std::vector<std::byte>& out, float v) {
    uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    PutU32(out, bits);
}

void PutText(std::vector<std::byte>& out, std::string_view text) {
    const auto* begin = reinterpret_cast<const std::byte*>(text.data());
    out.insert(out.end(), begin, begin + text.size());
}

}

Writer& Writer::AddBlob(std::string name, std::vector<std::byte> bytes) {
    return AddEntry(std::move(name), Kind::Blob, std::move(bytes));
}

Writer& Writer::AddImage(std::string name, uint16_t width, uint16_t height, const std::vector<std::byte>& rgba) {
    std::vector<std::byte> payload;
    PutU16(payload, width);
    PutU16(payload, height);
    payload.insert(payload.end(), rgba.begin(), rgba.end());
    return AddEntry(std::move(name), Kind::Image, std::move(payload));
}

Writer& Writer::AddText(std::string name, std::string_view text) {
    std::vector<std::byte> payload;
    PutText(payload, text);
    return AddEntry(std::move(name), Kind::Text, std::move(payload));
}

Writer& Writer::AddMesh(std::string name, std::array<float, 4> baseColor, std::array<float, 3> position,
                        std::array<float, 3> halfExtents) {
    std::vector<std::byte> payload;
    for (float v : baseColor) PutF32(payload, v);
    for (float v : position) PutF32(payload, v);
    for (float v : halfExtents) PutF32(payload, v);
    PutF32(payload, 0.0f);
    PutF32(payload, 0.0f);
    return AddEntry(std::move(name), Kind::Mesh, std::move(payload));
}

Writer& Writer::AddTable(std::string name, const std::vector<std::vector<std::string>>& rows) {
    const size_t cols = rows.empty() ? 0 : rows.front().size();
    std::vector<std::byte> payload;
    PutU16(payload, static_cast<uint16_t>(cols));
    PutU16(payload, static_cast<uint16_t>(rows.size()));
    for (const std::vector<std::string>& row : rows) {
        for (size_t c = 0; c < cols; ++c) {
            const std::string_view cell = c < row.size() ? std::string_view(row[c]) : std::string_view();
            PutU16(payload, static_cast<uint16_t>(cell.size()));
            PutText(payload, cell);
        }
    }
    return AddEntry(std::move(name), Kind::Table, std::move(payload));
}

Writer& Writer::AddEntry(std::string name, Kind kind, std::vector<std::byte> payload) {
    Item item;
    item.name = std::move(name);
    item.kind = kind;
    item.size = static_cast<uint32_t>(payload.size());
    item.payload = std::move(payload);
    m_items.push_back(std::move(item));
    return *this;
}

Writer& Writer::AddRange(std::string name, Kind kind, uint32_t offset, uint32_t size) {
    Item item;
    item.name = std::move(name);
    item.kind = kind;
    item.declared = true;
    item.offset = offset;
    item.size = size;
    m_items.push_back(std::move(item));
    return *this;
}

std::vector<std::byte> Writer::Build() const {
    size_t tocSize = 0;
    for (const Item& item : m_items) tocSize += 2 + item.name.size() + 1 + 4 + 4;

    std::vector<std::byte> out;
    PutText(out, kBoxMagic);
    PutU32(out, static_cast<uint32_t>(m_items.size()));

    uint32_t cursor = static_cast<uint32_t>(out.size() + tocSize);
    for (const Item& item : m_items) {
        PutU16(out, static_cast<uint16_t>(item.name.size()));
        PutText(out, item.name);
        PutU8(out, static_cast<uint8_t>(item.kind));
        if (item.declared) {
            PutU32(out, item.offset);
        } else {
            PutU32(out, cursor);
            cursor += item.size;
        }
        PutU32(out, item.size);
    }
    for (const Item& item : m_items) {
        if (!item.declared) out.insert(out.end(), item.payload.begin(), item.payload.end());
    }
    return out;
}

std::vector<std::byte> Writer::BuildPak(const std::vector<std::pair<std::string, std::vector<std::byte>>>& files) {
    std::vector<std::byte> out;
    PutText(out, kPakMagic);
    PutU32(out, static_cast<uint32_t>(files.size()));
    for (const auto& [name, bytes] : files) {
        PutU32(out, static_cast<uint32_t>(name.size()));
        PutText(out, name);
        PutU32(out, static_cast<uint32_t>(bytes.size()));
        out.insert(out.end(), bytes.begin(), bytes.end());
    }
    return out;
}

}
