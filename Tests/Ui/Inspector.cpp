#include <doctest/doctest.h>

#include <Onyx/Core/Schema/Schema.h>
#include <Onyx/Ui/Panels/Inspector.h>

#include <memory>

#include "Support/ObxWorkspace.h"

using namespace Onyx;
using namespace Onyx::Ui;

TEST_CASE("Inspector describes the type and capabilities of an image entry") {
    ObxUi fx("inspector_image");
    UiContext ui = fx.Make();

    const InspectorModel model = Inspector::Describe(Selection{fx.obx.doc, {{0}}}, ui);
    REQUIRE(model.valid);
    CHECK(model.name == "img");
    CHECK(model.typeKey.find("image") != std::string::npos);
    CHECK(model.media == "Image");
    CHECK(model.capabilities.image);
    CHECK_FALSE(model.capabilities.table);
    CHECK(model.capabilitiesText.find("image") != std::string::npos);
    CHECK(model.range.size == 4 + 8 * 8 * 4);
    CHECK(model.moduleId == "obx");
    CHECK_FALSE(model.moduleName.empty());
    CHECK(model.schema.empty());
}

TEST_CASE("Inspector lists the registered schema with offsets and sizes") {
    ObxUi fx("inspector_schema");
    UiContext ui = fx.Make();
    const Modules::Document* doc = fx.obx.workspace.Get(fx.obx.doc);
    const Types::TypeId type = doc->roots[0].type;

    auto inner = std::make_shared<Schema::Schema>("inner");
    inner->Field("a", Schema::FieldType::U8).Field("b", Schema::FieldType::U8);
    auto header = std::make_shared<Schema::Schema>("header");
    header->Field("magic", Schema::FieldType::U32)
        .Field("width", Schema::FieldType::U16)
        .Struct("inner", inner);
    fx.obx.workspace.Decoders().Schema(type, header);

    const InspectorModel model = Inspector::Describe(Selection{fx.obx.doc, {{0}}}, ui);
    REQUIRE(model.valid);
    CHECK(model.capabilities.schema);
    REQUIRE(model.schema.size() == 5);
    CHECK(model.schema[0].name == "magic");
    CHECK(model.schema[0].type == "u32");
    CHECK(model.schema[0].offset == 0);
    CHECK(model.schema[0].size == 4);
    CHECK(model.schema[1].offset == 4);
    CHECK(model.schema[2].name == "inner");
    CHECK(model.schema[2].size == 2);
    CHECK(model.schema[3].depth == 1);
    CHECK(model.schema[3].offset == 6);
    CHECK(model.schema[4].offset == 7);
}

TEST_CASE("Inspector is invalid for a document-only or unresolved selection") {
    ObxUi fx("inspector_invalid");
    UiContext ui = fx.Make();

    const InspectorModel docOnly = Inspector::Describe(Selection{fx.obx.doc, {}}, ui);
    CHECK_FALSE(docOnly.valid);
    CHECK(docOnly.moduleId == "obx");

    CHECK_FALSE(Inspector::Describe(Selection{fx.obx.doc, {{999}}}, ui).valid);
    CHECK_FALSE(Inspector::Describe(Selection{}, ui).valid);
    CHECK(Inspector::Describe(Selection{}, ui).moduleId.empty());
}
