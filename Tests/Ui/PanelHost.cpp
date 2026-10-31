#include <doctest/doctest.h>

#include "Support/BareContext.h"
#include "Support/HeadlessImGui.h"

#include <Onyx/Core/Services/Log.h>
#include <Onyx/Ui/PanelHost.h>

#include <stdexcept>

using namespace Onyx::Ui;

namespace {

class TestPanel : public IPanel {
public:
    TestPanel(std::string id, int& draws, bool throws = false) : m_id(std::move(id)), m_draws(draws), m_throws(throws) {}

    std::string_view Id() const override { return m_id; }
    std::string_view Title() const override { return "Title"; }
    void Draw(UiContext&) override {
        ++m_draws;
        if (m_throws) throw std::runtime_error("boom");
    }

private:
    std::string m_id;
    int& m_draws;
    bool m_throws;
};

void Frames(HeadlessImGui& rig, PanelHost& host, UiContext& ui, int count) {
    for (int i = 0; i < count; ++i) {
        rig.NewFrame();
        host.DrawAll(ui);
        rig.EndFrame();
    }
}

}

TEST_CASE("PanelHost draws visible panels only") {
    HeadlessImGui rig;
    BareContext bare;
    UiContext ui = bare.Make();
    PanelHost host;
    int shown = 0;
    int hidden = 0;
    host.Add(std::make_unique<TestPanel>("shown", shown));
    host.Add(std::make_unique<TestPanel>("hidden", hidden));
    host.Find("hidden")->visible = false;

    Frames(rig, host, ui, 1);
    CHECK(shown == 1);
    CHECK(hidden == 0);
}

TEST_CASE("Add replaces by id, Remove and Find") {
    PanelHost host;
    int draws = 0;
    host.Add(std::make_unique<TestPanel>("a", draws));
    host.Add(std::make_unique<TestPanel>("a", draws));
    CHECK(host.All().size() == 1);
    CHECK(host.Find("a") != nullptr);
    host.Remove("a");
    CHECK(host.Find("a") == nullptr);
    CHECK(host.All().empty());
}

TEST_CASE("A throwing panel is hidden after one frame and logged once") {
    HeadlessImGui rig;
    BareContext bare;
    UiContext ui = bare.Make();
    auto& log = Onyx::Services::Log::Default();
    int reports = 0;
    const auto count = [&reports](const Onyx::Services::LogEntry& e) {
        if (e.category == "ui.panel") ++reports;
    };
    const uint64_t sink = log.AddSink(count);

    PanelHost host;
    int bad = 0;
    int good = 0;
    host.Add(std::make_unique<TestPanel>("bad", bad, true));
    host.Add(std::make_unique<TestPanel>("good", good));

    Frames(rig, host, ui, 3);
    CHECK_FALSE(host.Find("bad")->visible);
    CHECK(bad == 1);
    CHECK(good == 3);
    CHECK(reports == 1);

    host.Find("bad")->visible = true;
    Frames(rig, host, ui, 3);
    CHECK(reports == 1);

    host.Add(std::make_unique<TestPanel>("bad", bad, true));
    Frames(rig, host, ui, 1);
    log.RemoveSink(sink);
    CHECK(reports == 2);
}

namespace {

class SelfRemovingPanel : public IPanel {
public:
    SelfRemovingPanel(PanelHost& host, int& draws) : m_host(host), m_draws(draws) {}

    std::string_view Id() const override { return "self"; }
    std::string_view Title() const override { return "Self"; }
    void Draw(UiContext&) override {
        ++m_draws;
        m_host.Remove("self");
        m_host.Add(std::make_unique<SelfRemovingPanel>(m_host, m_draws));
        m_host.Remove("self");
    }

private:
    PanelHost& m_host;
    int& m_draws;
};

}

TEST_CASE("A panel may remove itself while drawing") {
    HeadlessImGui rig;
    BareContext bare;
    UiContext ui = bare.Make();
    PanelHost host;
    int draws = 0;
    host.Add(std::make_unique<SelfRemovingPanel>(host, draws));

    Frames(rig, host, ui, 1);
    CHECK(draws == 1);
    CHECK(host.Find("self") == nullptr);
    Frames(rig, host, ui, 1);
    CHECK(draws == 1);
}

namespace {

class ForeignThrower : public IPanel {
public:
    std::string_view Id() const override { return "foreign"; }
    std::string_view Title() const override { return "Foreign"; }
    void Draw(UiContext&) override { throw 42; }
};

}

TEST_CASE("A panel throwing a non-std type is hidden and the host stays usable") {
    HeadlessImGui rig;
    BareContext bare;
    UiContext ui = bare.Make();
    PanelHost host;
    int good = 0;
    host.Add(std::make_unique<ForeignThrower>());
    host.Add(std::make_unique<TestPanel>("good", good));

    Frames(rig, host, ui, 1);
    CHECK_FALSE(host.Find("foreign")->visible);
    CHECK(good == 1);

    host.Remove("foreign");
    CHECK(host.Find("foreign") == nullptr);
    host.Add(std::make_unique<TestPanel>("late", good));
    CHECK(host.Find("late") != nullptr);
}
