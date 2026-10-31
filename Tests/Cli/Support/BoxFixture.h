#pragma once

#include <OnyxBox/Writer.h>

#include <cstddef>
#include <vector>

namespace Onyx::Tests {

inline std::vector<std::byte> Gradient(size_t count) {
    std::vector<std::byte> out(count);
    for (size_t i = 0; i < count; ++i) out[i] = static_cast<std::byte>(i * 7 & 0xFF);
    return out;
}

// An 8x8 image, a text entry, and an entry whose range lies outside the file.
inline std::vector<std::byte> SampleBox() {
    OnyxBox::Writer w;
    w.AddImage("img", 8, 8, Gradient(256));
    w.AddText("txt", "hello box");
    w.AddRange("bad", OnyxBox::Kind::Blob, 0x100000, 4);
    return w.Build();
}

// Two images share the name `img`.
inline std::vector<std::byte> AmbiguousBox() {
    OnyxBox::Writer w;
    w.AddImage("img", 8, 8, Gradient(256));
    w.AddText("txt", "hello box");
    w.AddImage("img", 8, 8, Gradient(256));
    return w.Build();
}

}
