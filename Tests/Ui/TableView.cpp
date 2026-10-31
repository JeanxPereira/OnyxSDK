#include <doctest/doctest.h>

#include <Onyx/Ui/Viewers/TableView.h>

#include "Support/ObxViewerFixture.h"

using namespace Onyx;
using namespace Onyx::Ui;

namespace {

TableView Make() {
    Domain::Table table;
    table.columns = {"id", "name"};
    table.rows = {{"10", "pear"}, {"9", "apple"}, {"100", "fig"}};
    return TableView(1, Modules::NodePath{{0}}, "t", std::move(table));
}

}

TEST_CASE("TableView exposes the table's columns and rows") {
    TableView view = Make();
    CHECK(view.Columns() == std::vector<std::string>{"id", "name"});
    CHECK(view.RowCount() == 3);
    CHECK(view.Cell(0, 1) == "pear");
    CHECK(view.Cell(5, 0).empty());
    CHECK(view.Cell(0, 5).empty());
}

TEST_CASE("TableView sorts numbers numerically and text lexically") {
    TableView view = Make();
    view.SortBy(0, true);
    CHECK(view.Cell(0, 0) == "9");
    CHECK(view.Cell(1, 0) == "10");
    CHECK(view.Cell(2, 0) == "100");

    view.SortBy(0, false);
    CHECK(view.Cell(0, 0) == "100");

    view.SortBy(1, true);
    CHECK(view.Cell(0, 1) == "apple");
    CHECK(view.Cell(2, 1) == "pear");

    view.ClearSort();
    CHECK(view.Cell(0, 1) == "pear");
}

TEST_CASE("TableView draws") {
    HeadlessImGui imgui;
    TestSupport::ObxViewerFixture fx;
    UiContext ui = fx.Make();
    TableView view = Make();
    TestSupport::DrawFrames(imgui, view, ui);
    TableView empty(1, Modules::NodePath{{0}}, "e", Domain::Table{});
    TestSupport::DrawFrames(imgui, empty, ui);
}
