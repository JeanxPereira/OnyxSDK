#pragma once

#include <Onyx/Core/Domain/Text.h>
#include <Onyx/Ui/Panel.h>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace Onyx::Ui {

// A read-only text viewer. The text is split into line offsets once; drawing is clipped to the
// visible lines.
class TextView : public IDocumentView {
public:
    TextView(Modules::DocumentId doc, Modules::NodePath node, std::string name, Domain::Text text);

    std::string_view Id() const override { return m_id; }
    std::string_view Title() const override { return m_name; }
    void Draw(UiContext& ui) override;
    Modules::DocumentId Document() const override { return m_doc; }
    Modules::NodePath Node() const override { return m_node; }

    size_t Lines() const { return m_starts.size(); }
    // Without the line terminator.
    std::string_view Line(size_t index) const;
    // The first line at or after `from` whose text contains `needle`, ignoring case; Lines() when none.
    size_t FindNext(std::string_view needle, size_t from) const;

    bool wrap = false;

private:
    float LineTop(size_t line, float rowHeight) const;
    void LayoutWrap(float width);

    Modules::DocumentId m_doc;
    Modules::NodePath m_node;
    std::string m_name;
    std::string m_id;
    Domain::Text m_text;
    std::vector<size_t> m_starts;
    char m_query[128] = {};
    size_t m_match = 0;
    bool m_hasMatch = false;
    size_t m_scrollTo = 0;
    bool m_scrollPending = false;
    float m_wrapWidth = -1.0f;
    std::vector<float> m_wrapTop;
};

}
