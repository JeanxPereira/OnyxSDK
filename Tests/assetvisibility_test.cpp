// ── AssetVisibility tests (doctest) ───────────────────────────────────────
//
// The store is a process-wide singleton, so every case works on TypeIds minted
// for itself and clears its own overrides rather than resetting global state.

#include <doctest/doctest.h>

#include <Onyx/Domain/MediaKind.h>
#include <Onyx/Services/AssetVisibility.h>
#include <Onyx/Types/TypeCatalog.h>

#include <algorithm>
#include <string>

using namespace Onyx::Services;
using Onyx::Types::TypeCatalog;
using Onyx::Types::TypeId;

namespace {

TypeId MintType(const char* key) {
    Onyx::Types::TypeInfo info;
    info.key   = key;
    info.label = key;
    info.media = Onyx::Domain::MediaKind::Raw;
    return TypeCatalog::Get().Register(info);
}

bool HasOverrideFor(const std::vector<AssetVisibility::SerializedOverride>& all, TypeId id) {
    return std::any_of(all.begin(), all.end(), [&](const AssetVisibility::SerializedOverride& o) {
        return o.typeId == uint16_t(id.value);
    });
}

} // namespace

TEST_CASE("AssetVisibility defaults to visible and honours per-type defaults") {
    AssetVisibility& vis = AssetVisibility::Get();
    const TypeId fresh    = MintType("TEST_VIS_FRESH");
    const TypeId hidden   = MintType("TEST_VIS_HIDDEN");
    const TypeId internal = MintType("TEST_VIS_INTERNAL");

    CHECK(vis.GetDefault(fresh) == Visibility::Visible);
    CHECK(vis.IsVisible(fresh));

    vis.SetDefault(hidden, Visibility::Hidden);
    vis.SetDefault(internal, Visibility::Internal);

    CHECK(vis.GetCurrent(hidden) == Visibility::Hidden);
    CHECK_FALSE(vis.IsVisible(hidden));
    CHECK(vis.GetCurrent(internal) == Visibility::Internal);
    CHECK_FALSE(vis.IsVisible(internal));
}

TEST_CASE("AssetVisibility user overrides flip Hidden<->Visible but never Internal") {
    AssetVisibility& vis  = AssetVisibility::Get();
    const TypeId hidden   = MintType("TEST_VIS_OVERRIDE_HIDDEN");
    const TypeId internal = MintType("TEST_VIS_OVERRIDE_INTERNAL");
    vis.SetDefault(hidden, Visibility::Hidden);
    vis.SetDefault(internal, Visibility::Internal);

    vis.SetUserOverride(hidden, true);
    CHECK(vis.IsVisible(hidden));
    CHECK(HasOverrideFor(vis.ExportOverrides(), hidden));

    // Setting an override back to the default drops it rather than storing a
    // redundant entry -- otherwise the config would grow with every toggle.
    vis.SetUserOverride(hidden, false);
    CHECK_FALSE(vis.IsVisible(hidden));
    CHECK_FALSE(HasOverrideFor(vis.ExportOverrides(), hidden));

    // Internal types are structural: an override must not make them appear.
    vis.SetUserOverride(internal, true);
    CHECK_FALSE(vis.IsVisible(internal));
    CHECK_FALSE(HasOverrideFor(vis.ExportOverrides(), internal));

    vis.ClearUserOverride(hidden);
    CHECK(vis.GetCurrent(hidden) == Visibility::Hidden);
}

TEST_CASE("AssetVisibility overrides survive an export/import round-trip") {
    AssetVisibility& vis = AssetVisibility::Get();
    const TypeId a = MintType("TEST_VIS_RT_A");
    const TypeId b = MintType("TEST_VIS_RT_B");
    vis.SetDefault(a, Visibility::Hidden);
    vis.SetDefault(b, Visibility::Visible);

    vis.ResetAllOverrides();
    vis.SetUserOverride(a, true);   // show a normally-hidden type
    vis.SetUserOverride(b, false);  // hide a normally-visible one

    const auto exported = vis.ExportOverrides();
    CHECK(exported.size() == 2);

    vis.ResetAllOverrides();
    CHECK_FALSE(vis.IsVisible(a));
    CHECK(vis.IsVisible(b));

    vis.ImportOverrides(exported);
    CHECK(vis.IsVisible(a));
    CHECK_FALSE(vis.IsVisible(b));

    vis.ResetAllOverrides();
}
