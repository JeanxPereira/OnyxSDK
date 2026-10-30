// ── AssetVisibility tests (doctest) ───────────────────────────────────────
//
// The store is a process-wide singleton, so every case works on TypeIds minted
// for itself and clears its own overrides rather than resetting global state.

#include <doctest/doctest.h>

#include <Onyx/Domain/MediaKind.h>
#include <Onyx/Services/AssetVisibility.h>
#include <Onyx/Services/Settings.h>
#include <Onyx/Types/TypeCatalog.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>

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

bool HasOverrideForKey(const std::vector<std::pair<std::string, bool>>& all, std::string_view key) {
    return std::any_of(all.begin(), all.end(), [&](const std::pair<std::string, bool>& kv) {
        return kv.first == key;
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
    CHECK(HasOverrideForKey(vis.ExportOverridesByKey(TypeCatalog::Get()), TypeCatalog::Get().KeyOf(hidden)));

    // Setting an override back to the default drops it rather than storing a
    // redundant entry -- otherwise the config would grow with every toggle.
    vis.SetUserOverride(hidden, false);
    CHECK_FALSE(vis.IsVisible(hidden));
    CHECK_FALSE(HasOverrideForKey(vis.ExportOverridesByKey(TypeCatalog::Get()), TypeCatalog::Get().KeyOf(hidden)));

    // Internal types are structural: an override must not make them appear.
    vis.SetUserOverride(internal, true);
    CHECK_FALSE(vis.IsVisible(internal));
    CHECK_FALSE(HasOverrideForKey(vis.ExportOverridesByKey(TypeCatalog::Get()), TypeCatalog::Get().KeyOf(internal)));

    vis.ClearUserOverride(hidden);
    CHECK(vis.GetCurrent(hidden) == Visibility::Hidden);
}

TEST_CASE("AssetVisibility overrides persist by key via Settings, dropping unknown keys") {
    AssetVisibility& vis = AssetVisibility::Get();
    vis.ResetAllOverrides();

    const TypeId a = MintType("TEST_VIS_KEY_A");
    const TypeId b = MintType("TEST_VIS_KEY_B");
    vis.SetDefault(a, Visibility::Hidden);
    vis.SetDefault(b, Visibility::Visible);

    vis.SetUserOverride(a, true);   // show a normally-hidden type
    vis.SetUserOverride(b, false);  // hide a normally-visible one

    auto tmp = std::filesystem::temp_directory_path() / "onyx_visibility_overrides_test.toml";
    std::filesystem::remove(tmp);
    Settings settings = Settings::Load(tmp);

    vis.SaveOverrides(TypeCatalog::Get(), settings);

    // A key nothing has ever registered a type under: LoadOverrides must
    // drop it silently instead of resurrecting a garbage override, because
    // it walks the catalog's registered types rather than the file's keys.
    settings.Set("visibility.TEST_VIS_KEY_NEVER_REGISTERED", true);

    vis.ResetAllOverrides();
    CHECK_FALSE(vis.IsVisible(a));
    CHECK(vis.IsVisible(b));

    vis.LoadOverrides(TypeCatalog::Get(), settings);
    CHECK(vis.IsVisible(a));      // survived by key
    CHECK_FALSE(vis.IsVisible(b));

    const auto restored = vis.ExportOverridesByKey(TypeCatalog::Get());
    CHECK(restored.size() == 2);  // exactly a and b -- the unknown key never landed
    CHECK(TypeCatalog::Get().Find("TEST_VIS_KEY_NEVER_REGISTERED") == TypeId{});

    vis.ResetAllOverrides();
    std::filesystem::remove(tmp);
}
