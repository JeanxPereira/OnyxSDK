#include "Onyx/Ui/Panels/Inspector.h"

#include <Onyx/Core/Domain/AssetEntry.h>
#include <Onyx/Core/Modules/Workspace.h>
#include <Onyx/Core/Schema/Schema.h>
#include <Onyx/Ui/Panels/DocumentBrowser.h>
#include <Onyx/Ui/PanelHost.h>
#include <Onyx/Ui/UiContext.h>
#include <Onyx/Ui/Widgets.h>

#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace Onyx::Ui {

namespace {

using Schema::FieldType;

const char* FieldTypeName(FieldType type) {
    switch (type) {
        case FieldType::U8: return "u8";
        case FieldType::I8: return "i8";
        case FieldType::U16: return "u16";
        case FieldType::I16: return "i16";
        case FieldType::U32: return "u32";
        case FieldType::I32: return "i32";
        case FieldType::U64: return "u64";
        case FieldType::I64: return "i64";
        case FieldType::F32: return "f32";
        case FieldType::F64: return "f64";
        case FieldType::String: return "string";
        case FieldType::Bytes: return "bytes";
        case FieldType::Struct: return "struct";
        case FieldType::Array: return "array";
        case FieldType::Pad: return "pad";
    }
    return "";
}

std::optional<uint64_t> ScalarSize(FieldType type) {
    switch (type) {
        case FieldType::U8:
        case FieldType::I8: return 1;
        case FieldType::U16:
        case FieldType::I16: return 2;
        case FieldType::U32:
        case FieldType::I32:
        case FieldType::F32: return 4;
        case FieldType::U64:
        case FieldType::I64:
        case FieldType::F64: return 8;
        default: return std::nullopt;
    }
}

// Appends the rows of `schema`, laid out from `base`. Returns the end of the struct, or nullopt
// once a field's position depends on data.
std::optional<uint64_t> Flatten(const Schema::Schema& schema, int depth, uint64_t base,
                                std::vector<InspectorSchemaRow>& out) {
    std::optional<uint64_t> cursor = 0;
    for (const Schema::FieldDef& field : schema.Fields()) {
        std::optional<uint64_t> offset;
        switch (field.offset.kind) {
            case Schema::Offset::Kind::Next: offset = cursor; break;
            case Schema::Offset::Kind::Absolute: offset = field.offset.value; break;
            case Schema::Offset::Kind::FromStructStart: offset = field.offset.value; break;
            case Schema::Offset::Kind::FromField: break;
        }

        InspectorSchemaRow row;
        row.depth = depth;
        row.name = field.type == FieldType::Pad ? "(pad)" : field.name;
        row.type = FieldTypeName(field.type);
        if (offset) row.offset = base + *offset;

        std::optional<uint64_t> size = ScalarSize(field.type);
        if (field.type == FieldType::String || field.type == FieldType::Bytes || field.type == FieldType::Pad) {
            size = field.length;
        }
        const size_t rowIndex = out.size();
        out.push_back(row);

        if (field.element && (field.type == FieldType::Struct || field.type == FieldType::Array)) {
            const std::optional<uint64_t> inner =
                Flatten(*field.element, depth + 1, offset ? base + *offset : 0, out);
            if (field.type == FieldType::Struct) {
                size = inner;
            } else if (inner && field.count.kind == Schema::Count::Kind::Constant) {
                size = *inner * field.count.value;
            }
        }
        out[rowIndex].size = size;
        cursor = (offset && size) ? std::optional<uint64_t>(*offset + *size) : std::nullopt;
    }
    return cursor;
}

std::string CapabilitiesText(const Modules::Capabilities& caps) {
    std::string text;
    const auto add = [&](bool on, const char* name) {
        if (!on) return;
        if (!text.empty()) text += ", ";
        text += name;
    };
    add(caps.scene, "scene");
    add(caps.image, "image");
    add(caps.text, "text");
    add(caps.audio, "audio");
    add(caps.table, "table");
    add(caps.schema, "schema");
    for (const std::string& name : caps.any) add(true, name.c_str());
    return text.empty() ? "none" : text;
}

}

InspectorModel Inspector::Describe(const Selection& selection, UiContext& ui) {
    InspectorModel model;
    const Modules::Document* doc = ui.workspace.Get(selection.doc);
    if (!doc) return model;

    if (doc->module) {
        const Modules::ModuleInfo info = doc->module->Info();
        model.moduleId = info.id;
        model.moduleName = info.displayName;
    }
    if (!doc->ready.load()) return model;

    const Domain::AssetEntry* entry = Modules::Resolve(*doc, selection.node);
    if (!entry) return model;

    const Types::TypeSpec& spec = ui.workspace.Catalog().Spec(entry->type);
    model.valid = true;
    model.name = entry->name;
    model.typeKey = spec.key;
    model.typeLabel = spec.label;
    model.media = std::string(Domain::Name(spec.media));
    model.iconName = std::string(Types::Name(spec.icon));
    model.color = spec.color;
    model.range = entry->source;
    model.capabilities = ui.workspace.Decoders().Of(entry->type);
    model.capabilitiesText = CapabilitiesText(model.capabilities);
    if (const auto schema = ui.workspace.Decoders().SchemaOf(entry->type)) {
        Flatten(*schema, 0, 0, model.schema);
    }
    return model;
}

void Inspector::Draw(UiContext& ui) {
    const auto* browser = dynamic_cast<const DocumentBrowser*>(ui.panels.Find("onyx.browser"));
    if (!browser) {
        Widgets::Hint("No document browser");
        return;
    }
    const Selection& selection = browser->Selection().Current();
    if (!m_hasModel || !(selection == m_shownFor) || !m_model.valid) {
        m_model = Describe(selection, ui);
        m_shownFor = selection;
        m_hasModel = true;
    }
    if (!m_model.valid) {
        Widgets::Hint("Nothing selected");
        return;
    }

    const InspectorModel& m = m_model;
    ImGui::TextUnformatted(m.name.c_str());
    ImGui::SeparatorText("Type");
    ImGui::Text("Key: %s", m.typeKey.c_str());
    ImGui::Text("Label: %s", m.typeLabel.c_str());
    ImGui::Text("Media: %s", m.media.c_str());
    ImGui::Text("Icon: %s", m.iconName.c_str());
    ImGui::ColorButton("##typeColor", ImVec4(m.color.r, m.color.g, m.color.b, m.color.a),
                       ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker);

    ImGui::SeparatorText("Source");
    ImGui::Text("File %u, offset 0x%llx, size %llu", m.range.fileIndex,
                static_cast<unsigned long long>(m.range.offset), static_cast<unsigned long long>(m.range.size));

    ImGui::SeparatorText("Capabilities");
    ImGui::TextUnformatted(m.capabilitiesText.c_str());

    ImGui::SeparatorText("Module");
    ImGui::Text("%s (%s)", m.moduleName.c_str(), m.moduleId.c_str());

    if (!m.schema.empty()) {
        ImGui::SeparatorText("Schema");
        if (ImGui::BeginTable("schema", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Field");
            ImGui::TableSetupColumn("Type");
            ImGui::TableSetupColumn("Offset");
            ImGui::TableSetupColumn("Size");
            ImGui::TableHeadersRow();
            Widgets::ForEachVisible(m.schema.size(), ImGui::GetTextLineHeightWithSpacing(), [&](size_t i) {
                const InspectorSchemaRow& row = m.schema[i];
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Indent(static_cast<float>(row.depth) * ImGui::GetStyle().IndentSpacing);
                ImGui::TextUnformatted(row.name.c_str());
                ImGui::Unindent(static_cast<float>(row.depth) * ImGui::GetStyle().IndentSpacing);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(row.type.c_str());
                ImGui::TableNextColumn();
                if (row.offset) ImGui::Text("0x%llx", static_cast<unsigned long long>(*row.offset));
                else ImGui::TextUnformatted("-");
                ImGui::TableNextColumn();
                if (row.size) ImGui::Text("%llu", static_cast<unsigned long long>(*row.size));
                else ImGui::TextUnformatted("-");
            });
            ImGui::EndTable();
        }
    }
}

}
