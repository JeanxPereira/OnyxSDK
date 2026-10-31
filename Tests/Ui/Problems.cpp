#include <doctest/doctest.h>

#include <Onyx/Ui/Panels/DocumentBrowser.h>
#include <Onyx/Ui/Panels/Problems.h>

#include <memory>

#include "Support/ObxWorkspace.h"

using namespace Onyx;
using namespace Onyx::Ui;

TEST_CASE("Problems lists a diagnostic reported into a document") {
    ObxUi fx("problems_list");
    UiContext ui = fx.Make();
    Problems problems;
    CHECK(problems.Model(ui).rows.empty());

    Modules::Document* doc = fx.obx.workspace.Get(fx.obx.doc);
    doc->diags.Report({Services::Severity::Error, "t.boom", "it broke", std::nullopt});
    doc->diags.Report({Services::Severity::Warning, "t.warn", "careful", std::nullopt});

    const ProblemsModel& model = problems.Model(ui);
    REQUIRE(model.rows.size() == 2);
    CHECK(model.errors == 1);
    CHECK(model.warnings == 1);
    CHECK(model.rows[0].doc == fx.obx.doc);
    CHECK(model.rows[0].text.find("it broke") != std::string::npos);
    CHECK(model.rows[0].diag.code == "t.boom");
}

TEST_CASE("Problems severity filters hide rows but keep the totals") {
    ObxUi fx("problems_filter");
    UiContext ui = fx.Make();
    Problems problems;
    Modules::Document* doc = fx.obx.workspace.Get(fx.obx.doc);
    doc->diags.Report({Services::Severity::Error, "t.e", "e", std::nullopt});
    doc->diags.Report({Services::Severity::Info, "t.i", "i", std::nullopt});

    CHECK(problems.Model(ui).rows.size() == 2);
    problems.showErrors = false;
    const ProblemsModel& model = problems.Model(ui);
    REQUIRE(model.rows.size() == 1);
    CHECK(model.rows[0].diag.severity == Services::Severity::Info);
    CHECK(model.errors == 1);
}

TEST_CASE("Problems resolves a byte reference to its entry and navigates the browser") {
    ObxUi fx("problems_navigate");
    UiContext ui = fx.Make();
    const Modules::Document* doc = fx.obx.workspace.Get(fx.obx.doc);
    const Domain::ByteRange text = doc->roots[1].source;

    const auto path = Problems::FindEntry(*doc, Services::ByteRef{"fixture.obx", text.offset + 2});
    REQUIRE(path.has_value());
    CHECK(path->indices == std::vector<uint32_t>{1});
    CHECK_FALSE(Problems::FindEntry(*doc, Services::ByteRef{"fixture.obx", 0}).has_value());
    CHECK(Problems::FindEntry(*doc, Services::ByteRef{"", text.offset + 2}).has_value());
    CHECK_FALSE(Problems::FindEntry(*doc, Services::ByteRef{"other.bin", text.offset + 2}).has_value());

    fx.panels.Add(std::make_unique<DocumentBrowser>());
    auto* browser = dynamic_cast<DocumentBrowser*>(fx.panels.Find("onyx.browser"));
    REQUIRE(browser);
    browser->Refresh(ui);
    browser->Select(ui, Selection{fx.obx.doc, *path});
    CHECK(browser->Selection().Current().node.indices == std::vector<uint32_t>{1});
}
