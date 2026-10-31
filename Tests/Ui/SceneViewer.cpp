#include <doctest/doctest.h>

#include <Onyx/TestKit/SceneFixtures.h>
#include <Onyx/Ui/Viewers/SceneView.h>

#include "Support/BareContext.h"

#include <memory>

using namespace Onyx;

TEST_CASE("RegisterSceneViewer registers onyx.scene for meshes and builds a view from a scene") {
    BareContext rig;
    Ui::RegisterSceneViewer(rig.viewers);
    REQUIRE(rig.viewers.All().size() == 1);
    const Ui::ViewerSpec& spec = rig.viewers.All()[0];
    CHECK(spec.id == "onyx.scene");
    CHECK(spec.priority == 0);
    REQUIRE(std::holds_alternative<Domain::MediaKind>(spec.target));
    CHECK(std::get<Domain::MediaKind>(spec.target) == Domain::MediaKind::Mesh);

    Ui::OpenRequest empty{.workspace = rig.workspace, .kind = Domain::MediaKind::Mesh, .name = "none"};
    CHECK(spec.create(empty) == nullptr);

    TestKit::SceneFixture cube = TestKit::BuildSkinnedCube();
    Ui::OpenRequest request{.workspace = rig.workspace,
                            .doc = 7,
                            .kind = Domain::MediaKind::Mesh,
                            .name = "cube",
                            .scene = std::make_unique<Domain::Scene>(std::move(cube.scene))};
    auto view = spec.create(request);
    REQUIRE(view != nullptr);
    CHECK(view->Document() == 7);
    CHECK(view->Title() == "cube");
    CHECK(request.scene == nullptr);
}
