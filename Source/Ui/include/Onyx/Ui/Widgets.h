#pragma once

#include <cstddef>

#include <glm/vec4.hpp>
#include <imgui.h>

#include "Onyx/Core/Services/Diagnostics.h"
#include "Onyx/Ui/Theme.h"

namespace Onyx::Ui::Widgets {

struct IconButtonOptions {
    ImVec2 size{};
    const char* tooltip = nullptr;
    bool selected = false;
    bool disabled = false;
};

// A framed button whose glyph is centred on its ink, not on its advance.
bool IconButton(const char* id, const char* glyph, const IconButtonOptions& options = {});

// A tree node whose label carries a leading glyph and a base colour; the colour yields to a
// readable one while the row is hovered or selected.
bool TreeRow(const char* id, const char* label, const char* glyph, const glm::vec4& color,
             ImGuiTreeNodeFlags flags, bool selected);

bool ToolbarButton(const char* label, bool selected = false);

void Hint(const char* text);

void Spinner(const char* id, float radius, float thickness);

// Horizontal splitter between two sizes. Dragging moves the boundary, both sides keep their
// minimum and left + right stays constant. Returns true on the frames the boundary moves.
bool SplitterH(const char* id, float* left, float* right, float minLeft, float minRight);

void StatusText(Services::Severity severity, const char* text);

struct Metrics {
    float smallButtonInset;
    float iconButtonSize;
    float rowHeight;
};
Metrics Metrics(const Theme& theme);

// Calls draw(index) for the rows inside the visible range only.
template <class F>
void ForEachVisible(size_t count, float rowHeight, F&& draw) {
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(count), rowHeight);
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            draw(static_cast<size_t>(i));
        }
    }
}

}
