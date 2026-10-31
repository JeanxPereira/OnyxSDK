#include <doctest/doctest.h>

#include <GLFW/glfw3.h>

#include <filesystem>
#include <vector>

#include "Onyx/Shell/NativeWindow.h"

using namespace Onyx;

namespace {

Shell::WindowOptions Hidden(bool borderless) {
    Shell::WindowOptions options;
    options.title = "Onyx.Shell.NativeWindow";
    options.size = {320, 200};
    options.borderless = borderless;
    options.visible = false;
    return options;
}

}

TEST_CASE("A hidden window reports the framebuffer size it was created with") {
    for (const bool borderless : {true, false}) {
        CAPTURE(borderless);
        auto window = Shell::NativeWindow::Create(Hidden(borderless));
        REQUIRE_MESSAGE(window.ok(), window.error().code << ": " << window.error().message);
        const glm::ivec2 size = window.value()->FramebufferSize();
        if (borderless) {
            CHECK(size == glm::ivec2(320, 200));
        } else {
            CHECK(size.x > 0);
            CHECK(size.y > 0);
        }
        CHECK(window.value()->ContentScale() > 0.0f);
        CHECK_FALSE(window.value()->ShouldClose());
        window.value()->SetShouldClose(true);
        CHECK(window.value()->ShouldClose());
    }
}

TEST_CASE("Only Windows makes a borderless window frameless") {
#if defined(_WIN32)
    CHECK(Shell::WindowOptions{}.borderless);
#else
    CHECK_FALSE(Shell::WindowOptions{}.borderless);
#endif
    for (const bool borderless : {true, false}) {
        CAPTURE(borderless);
        auto window = Shell::NativeWindow::Create(Hidden(borderless));
        REQUIRE_MESSAGE(window.ok(), window.error().code << ": " << window.error().message);
        const bool decorated = glfwGetWindowAttrib(window.value()->Handle(), GLFW_DECORATED) == GLFW_TRUE;
#if defined(_WIN32)
        CHECK(decorated == !borderless);
#else
        CHECK(decorated);
#endif
    }
}

TEST_CASE("The window names the instance extensions a surface needs") {
    auto window = Shell::NativeWindow::Create(Hidden(true));
    REQUIRE(window.ok());
    CHECK_FALSE(window.value()->RequiredInstanceExtensions().empty());
}

TEST_CASE("Windows come and go while GLFW stays initialised for the live ones") {
    auto first = Shell::NativeWindow::Create(Hidden(false));
    REQUIRE(first.ok());
    {
        auto second = Shell::NativeWindow::Create(Hidden(false));
        REQUIRE(second.ok());
    }
    first.value()->PollEvents();
    CHECK(first.value()->FramebufferSize().x > 0);
}

TEST_CASE("SimulateDrop reaches the registered drop handler") {
    auto window = Shell::NativeWindow::Create(Hidden(false));
    REQUIRE(window.ok());
    window.value()->SimulateDrop({"ignored"});

    std::vector<std::filesystem::path> received;
    window.value()->SetDropHandler([&](std::vector<std::filesystem::path> paths) { received = std::move(paths); });
    const std::filesystem::path accented = std::filesystem::path(u8"dir") / std::filesystem::path(u8"caf\u00e9.bin");
    window.value()->SimulateDrop({"a.bin", accented});
    REQUIRE(received.size() == 2);
    const bool first = received[0] == std::filesystem::path("a.bin");
    const bool second = received[1] == accented;
    const bool leaf = received[1].filename().u8string() == std::u8string(u8"caf\u00e9.bin");
    CHECK(first);
    CHECK(second);
    CHECK(leaf);
}

TEST_CASE("The resize handler fires when the window is resized") {
    auto window = Shell::NativeWindow::Create(Hidden(false));
    REQUIRE(window.ok());
    glm::ivec2 last{0, 0};
    window.value()->SetResizeHandler([&](glm::ivec2 size) { last = size; });
    glfwSetWindowSize(window.value()->Handle(), 410, 260);
    window.value()->PollEvents();
    CHECK(last.x > 0);
    CHECK(last.y > 0);
}

TEST_CASE("The caption rectangle is stored as given") {
    auto window = Shell::NativeWindow::Create(Hidden(true));
    REQUIRE(window.ok());
    window.value()->SetTitleBarRect({0.0f, 0.0f, 320.0f, 32.0f, true});
    const Shell::TitleBarRect& rect = window.value()->GetTitleBarRect();
    CHECK(rect.width == 320.0f);
    CHECK(rect.height == 32.0f);
    CHECK(rect.blocked);
}
