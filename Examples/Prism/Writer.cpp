#include <Prism/Writer.h>

#include <cstring>

namespace Prism {

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

Writer& Writer::SetPalette(std::vector<glm::u8vec4> colors) {
    m_palette = std::move(colors);
    m_hasPalette = true;
    return *this;
}

Writer& Writer::Add(std::string name, Kind kind, std::vector<std::byte> payload) {
    Item item;
    item.name = std::move(name);
    item.kind = kind;
    item.size = static_cast<uint32_t>(payload.size());
    item.payload = std::move(payload);
    m_items.push_back(std::move(item));
    return *this;
}

Writer& Writer::AddImage(std::string name, uint16_t width, uint16_t height, std::vector<uint8_t> indices) {
    std::vector<std::byte> payload;
    PutU16(payload, width);
    PutU16(payload, height);
    for (uint8_t index : indices) PutU8(payload, index);
    return Add(std::move(name), Kind::Image, std::move(payload));
}

Writer& Writer::AddManifest(std::string name, std::string_view text) {
    std::vector<std::byte> payload;
    PutText(payload, text);
    return Add(std::move(name), Kind::Manifest, std::move(payload));
}

Writer& Writer::AddClip(std::string name, uint32_t sampleRate, uint16_t channels,
                        const std::vector<int16_t>& samples) {
    std::vector<std::byte> payload;
    PutU32(payload, sampleRate);
    PutU16(payload, channels);
    PutU32(payload, channels ? static_cast<uint32_t>(samples.size() / channels) : 0);
    for (int16_t s : samples) PutU16(payload, static_cast<uint16_t>(s));
    return Add(std::move(name), Kind::Clip, std::move(payload));
}

Writer& Writer::AddEntries(std::string name, const std::vector<std::vector<std::string>>& rows) {
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
    return Add(std::move(name), Kind::Entries, std::move(payload));
}

Writer& Writer::AddStats(std::string name, uint32_t imageCount, uint32_t clipCount, float gainDefault,
                         uint32_t flags) {
    std::vector<std::byte> payload;
    PutU32(payload, imageCount);
    PutU32(payload, clipCount);
    PutF32(payload, gainDefault);
    PutU32(payload, flags);
    return Add(std::move(name), Kind::Stats, std::move(payload));
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
    std::vector<Item> items;
    if (m_hasPalette) {
        Item palette;
        palette.name = "palette";
        palette.kind = Kind::Palette;
        for (const glm::u8vec4& c : m_palette) {
            PutU8(palette.payload, c.r);
            PutU8(palette.payload, c.g);
            PutU8(palette.payload, c.b);
            PutU8(palette.payload, c.a);
        }
        palette.size = static_cast<uint32_t>(palette.payload.size());
        items.push_back(std::move(palette));
    }
    items.insert(items.end(), m_items.begin(), m_items.end());

    size_t headerSize = kFileHeaderSize;
    for (const Item& item : items) headerSize += kMinSectionHeader + item.name.size();

    std::vector<std::byte> out;
    PutText(out, kMagic);
    PutU16(out, static_cast<uint16_t>(m_hasPalette ? m_palette.size() : 0));
    PutU32(out, static_cast<uint32_t>(items.size()));

    auto cursor = static_cast<uint32_t>(headerSize);
    for (const Item& item : items) {
        PutU8(out, static_cast<uint8_t>(item.kind));
        PutU16(out, static_cast<uint16_t>(item.name.size()));
        PutText(out, item.name);
        if (item.declared) {
            PutU32(out, item.offset);
        } else {
            PutU32(out, cursor);
            cursor += item.size;
        }
        PutU32(out, item.size);
    }
    for (const Item& item : items) {
        if (!item.declared) out.insert(out.end(), item.payload.begin(), item.payload.end());
    }
    return out;
}

}
