#include <doctest/doctest.h>

#include <Onyx/Core/Services/Log.h>
#include <Onyx/Ui/Panels/LogPanel.h>

#include <string>

#include "Support/HeadlessImGui.h"
#include "Support/ObxWorkspace.h"

using namespace Onyx;
using namespace Onyx::Ui;

TEST_CASE("LogPanel receives entries written to the default log and stops after destruction") {
    const std::string marker = "logpanel-marker-7f3a";
    {
        LogPanel panel;
        Services::Log::Default().Write(Services::LogLevel::Warn, "ui.test", marker);
        bool found = false;
        for (const std::string& line : panel.Lines()) found = found || line.find(marker) != std::string::npos;
        CHECK(found);
    }
    Services::Log::Default().Write(Services::LogLevel::Warn, "ui.test", marker);
}

TEST_CASE("LogPanel drops the oldest line past its capacity") {
    Services::Log log;
    LogPanel panel(log);
    const size_t extra = 10;
    for (size_t i = 0; i < LogPanel::kCapacity + extra; ++i) {
        log.Write(Services::LogLevel::Info, "cat", std::string_view("line " + std::to_string(i) + "!"));
    }

    CHECK(panel.Size() == LogPanel::kCapacity);
    const std::vector<std::string> lines = panel.Lines();
    CHECK(lines.front().find("line 10!") != std::string::npos);
    CHECK(lines.back().find("line " + std::to_string(LogPanel::kCapacity + extra - 1) + "!") != std::string::npos);
}

TEST_CASE("LogPanel clears and draws with a level filter") {
    Services::Log log;
    LogPanel panel(log);
    log.Write(Services::LogLevel::Info, "cat", std::string_view("info line"));
    log.Write(Services::LogLevel::Error, "cat", std::string_view("error line"));
    CHECK(panel.Size() == 2);

    ObxUi fx("logpanel_draw");
    UiContext ui = fx.Make();
    HeadlessImGui rig;
    panel.minLevel = Services::LogLevel::Error;
    DrawFrames(rig, 3, [&] { panel.Draw(ui); });

    panel.Clear();
    CHECK(panel.Size() == 0);
    DrawFrames(rig, 2, [&] { panel.Draw(ui); });
}
