#pragma once

#include <Prism/Writer.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Onyx::Tests {

// Four colours, a 4x4 image whose rows all read 0 1 2 3, a manifest, a 0.1 s mono 8000 Hz ramp, a
// 3x2 entries table, a stats record, and an image whose range lies outside the file.
inline std::vector<std::byte> SamplePrism() {
    std::vector<uint8_t> indices;
    for (int row = 0; row < 4; ++row) {
        for (uint8_t column = 0; column < 4; ++column) indices.push_back(column);
    }
    std::vector<int16_t> ramp(800);
    for (size_t i = 0; i < ramp.size(); ++i) ramp[i] = static_cast<int16_t>(i * 40);

    Prism::Writer w;
    w.SetPalette({{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255}, {0, 0, 0, 0}});
    w.AddImage("tiles", 4, 4, indices);
    w.AddManifest("manifest", "title = \"sample\"\n");
    w.AddClip("clip", 8000, 1, ramp);
    w.AddEntries("entries", {{"id", "name", "note"}, {"1", "one", "first"}});
    w.AddStats("stats", 1, 1, 1.0f, 0);
    w.AddRange("bad", Prism::Kind::Image, 0x100000, 4);
    return w.Build();
}

}
