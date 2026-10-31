#include <doctest/doctest.h>

#include <Onyx/Core/Services/Settings.h>
#include <Onyx/Core/Vfs/OsFile.h>
#include <Onyx/Media/MediaView.h>
#include <Onyx/Media/Viewers.h>
#include <Onyx/Ui/Command.h>
#include <Onyx/Ui/FrameClock.h>
#include <Onyx/Ui/PanelHost.h>
#include <Onyx/Ui/Theme.h>
#include <Onyx/Ui/UiContext.h>
#include <Onyx/Ui/ViewerRegistry.h>

#include <imgui.h>

#include "HeadlessImGui.h"
#include "Support/ClipFixture.h"

#include <chrono>
#include <memory>
#include <thread>
#include <variant>

using namespace Onyx;
using namespace std::chrono_literals;

namespace {

struct ViewRig {
    Modules::Workspace workspace;
    Services::Settings settings = Services::Settings::Empty();
    Ui::Theme theme{settings};
    Ui::CommandRegistry commands;
    Ui::ViewerRegistry viewers;
    Ui::PanelHost panels;
    Ui::FrameClock clock;
    HeadlessImGui imgui;

    Ui::UiContext Make() {
        return Ui::UiContext{workspace, nullptr, nullptr, nullptr, theme, commands, viewers, panels, clock, 1.0f / 60};
    }

    // One frame with the view drawn in a fixed window; returns the height the view's content took.
    float DrawFrame(Media::MediaView& view) {
        Ui::UiContext ui = Make();
        imgui.NewFrame();
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(640, 480));
        ImGui::Begin("media", nullptr, ImGuiWindowFlags_NoSavedSettings);
        const float top = ImGui::GetCursorPosY();
        view.Draw(ui);
        const float height = ImGui::GetCursorPosY() - top;
        ImGui::End();
        imgui.EndFrame();
        return height;
    }
};

std::shared_ptr<const Vfs::IFile> OpenFile(const ClipFixture& clip) {
    auto file = Vfs::OsFile::Open(clip.path);
    REQUIRE_MESSAGE(file.ok(), file.error().code << ": " << file.error().message);
    return file.value();
}

Domain::ByteRange RangeOf(const ClipFixture& clip) {
    return Domain::ByteRange{0, clip.offset, clip.size};
}

}

TEST_CASE("MediaView draws the transport without a texture pool") {
    const ClipFixture clip = WriteClip("view", true);
    ViewRig rig;
    Media::MediaView view(3, {}, "clip", OpenFile(clip), RangeOf(clip));
    CHECK(view.Id() == "onyx.media.3.clip");
    CHECK(view.Title() == "clip");
    CHECK(view.Document() == 3);

    CHECK(rig.DrawFrame(view) > 0.0f);
    REQUIRE_FALSE(view.Fault().has_value());
    view.Source().SetVolume(0.0f);
    view.Source().Play();
    for (int i = 0; i < 10; ++i) {
        CHECK(rig.DrawFrame(view) > 0.0f);
        std::this_thread::sleep_for(10ms);
    }
    CHECK_FALSE(view.Fault().has_value());
    CHECK(view.Texture() == ImTextureID_Invalid);
    CHECK(view.Source().State() == Media::PlayState::Playing);
    CHECK(view.Source().Info().videoCodec == "mpeg1video");
}

TEST_CASE("destroying a playing MediaView stops within one second") {
    const ClipFixture clip = WriteClip("view-destroy", true);
    ViewRig rig;
    auto view = std::make_unique<Media::MediaView>(3, Modules::NodePath{}, "clip", OpenFile(clip), RangeOf(clip));
    rig.DrawFrame(*view);
    view->Source().SetVolume(0.0f);
    view->Source().Play();
    for (int i = 0; i < 3; ++i) rig.DrawFrame(*view);
    std::this_thread::sleep_for(50ms);

    const auto start = std::chrono::steady_clock::now();
    view.reset();
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    CHECK(seconds < 1.0);
}

TEST_CASE("MediaView shows the open failure as status text") {
    const ClipFixture clip = WriteClip("view-bad", false);
    ViewRig rig;
    Media::MediaView view(3, {}, "clip", OpenFile(clip), Domain::ByteRange{0, 0, 2048});
    rig.DrawFrame(view);
    REQUIRE(view.Fault().has_value());
    CHECK(view.Fault()->code == "media.open");
}

TEST_CASE("RegisterMediaViewer routes video to a MediaView over the request's file") {
    const ClipFixture clip = WriteClip("view-register", false);
    ViewRig rig;
    Media::RegisterMediaViewer(rig.viewers);
    REQUIRE(rig.viewers.All().size() == 1);
    const Ui::ViewerSpec& spec = rig.viewers.All()[0];
    CHECK(spec.id == "onyx.media");
    CHECK(spec.priority == 0);
    CHECK(spec.anyCapability.empty());
    REQUIRE(std::holds_alternative<Domain::MediaKind>(spec.target));
    CHECK(std::get<Domain::MediaKind>(spec.target) == Domain::MediaKind::Video);

    Types::TypeSpec movieType;
    movieType.key = "movie";
    movieType.label = "movie";
    movieType.media = Domain::MediaKind::Video;
    const Types::TypeId type = rig.workspace.Catalog().Register("test", movieType);
    const Ui::ViewerSpec* routed = rig.viewers.Route(rig.workspace.Catalog(), rig.workspace.Decoders(), type);
    REQUIRE(routed == &spec);

    Ui::OpenRequest request{.workspace = rig.workspace,
                            .doc = 7,
                            .type = type,
                            .kind = Domain::MediaKind::Video,
                            .name = "movie",
                            .file = OpenFile(clip),
                            .range = RangeOf(clip)};
    auto view = routed->create(request);
    REQUIRE(view != nullptr);
    CHECK(view->Id() == "onyx.media.7.movie");
    auto* media = dynamic_cast<Media::MediaView*>(view.get());
    REQUIRE(media != nullptr);
    rig.DrawFrame(*media);
    CHECK_FALSE(media->Fault().has_value());
    CHECK(media->Source().Info().width == 64);
}

TEST_CASE("MediaView asks the clock for another frame while playing and not while paused") {
    const ClipFixture clip = WriteClip("view-clock", true);
    ViewRig rig;
    Media::MediaView view(3, {}, "clip", OpenFile(clip), RangeOf(clip));
    rig.clock.BeginFrame(1.0);
    rig.DrawFrame(view);
    REQUIRE_FALSE(view.Fault().has_value());
    CHECK_FALSE(rig.clock.WantsContinuous());

    view.Source().SetVolume(0.0f);
    view.Source().Play();
    rig.DrawFrame(view);
    CHECK(rig.clock.WantsContinuous());

    rig.clock.BeginFrame(2.0);
    view.Source().Pause();
    rig.DrawFrame(view);
    CHECK_FALSE(rig.clock.WantsContinuous());
}

TEST_CASE("MediaView over an empty range fails with media.open and opens no source") {
    const ClipFixture clip = WriteClip("view-empty", false);
    ViewRig rig;
    Media::MediaView view(3, {}, "clip", OpenFile(clip), Domain::ByteRange{0, clip.offset, 0});
    rig.DrawFrame(view);
    REQUIRE(view.Fault().has_value());
    CHECK(view.Fault()->code == "media.open");
    CHECK(view.Source().Info().videoCodec.empty());
    CHECK(view.Source().State() == Media::PlayState::Stopped);
}
