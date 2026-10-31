#pragma once

#include <Onyx/Core/Domain/ByteRange.h>
#include <Onyx/Core/Modules/Capabilities.h>
#include <Onyx/Core/Types/Icons.h>
#include <Onyx/Ui/Panel.h>
#include <Onyx/Ui/Selection.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Onyx::Ui {

struct InspectorSchemaRow {
    int depth = 0;
    std::string name;
    std::string type;
    std::optional<uint64_t> offset;
    std::optional<uint64_t> size;
};

// What the inspector shows for one selection. `valid` is false when the selection does not
// resolve to an entry; the module fields are still filled when the document is known.
struct InspectorModel {
    bool valid = false;
    std::string moduleId;
    std::string moduleName;
    std::string name;
    std::string typeKey;
    std::string typeLabel;
    std::string media;
    std::string iconName;
    Types::Color color;
    Domain::ByteRange range;
    Modules::Capabilities capabilities;
    std::string capabilitiesText;
    std::vector<InspectorSchemaRow> schema;
};

class Inspector final : public IPanel {
public:
    std::string_view Id() const override { return "onyx.inspector"; }
    std::string_view Title() const override { return "Inspector"; }
    void Draw(UiContext& ui) override;

    static InspectorModel Describe(const Selection& selection, UiContext& ui);

private:
    Selection m_shownFor;
    InspectorModel m_model;
    bool m_hasModel = false;
};

}
