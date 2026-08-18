// ── ProfileManager tests (doctest) ────────────────────────────────────────
//
// The engine ships no profiles of its own -- apps register them -- so these
// cases drive the manager with stub profiles that only implement detection and
// hints. The manager is a singleton with no removal API, so the stubs are
// registered exactly once and every case reasons about that fixed set.

#include <doctest/doctest.h>

#include <Onyx/Domain/IAssetProfile.h>
#include <Onyx/Services/ProfileManager.h>

#include <memory>
#include <string>

using namespace Onyx::Services;
using Onyx::Domain::IAssetProfile;
using Onyx::Domain::OpenFilter;

namespace {

class StubProfile : public IAssetProfile {
public:
    StubProfile(std::string name, std::string extension, std::vector<std::string> hints)
        : m_name(std::move(name)), m_ext(std::move(extension)), m_hints(std::move(hints)) {}

    std::string GetName() const override { return m_name; }

    bool Detect(const std::filesystem::path& path) const override {
        return path.extension() == m_ext;
    }

    std::shared_ptr<Onyx::Vfs::IVirtualFileSystem>
    MountArchive(const std::filesystem::path&) override { return nullptr; }

    bool ParseContainer(std::shared_ptr<Onyx::Vfs::IFile>, Onyx::Domain::AssetContainer&) override {
        return false;
    }

    bool LoadFromArchive(std::shared_ptr<Onyx::Vfs::IVirtualFileSystem>,
                         Onyx::Domain::AssetContainer&) override {
        return false;
    }

    OpenFilter GetOpenFilter() const override {
        return OpenFilter{m_name, {m_ext.substr(1)}};
    }

    std::vector<std::string> GetHints() const override { return m_hints; }

private:
    std::string m_name;
    std::string m_ext;
    std::vector<std::string> m_hints;
};

// Registers the stubs on first use so repeated TEST_CASEs share one set.
void EnsureStubsRegistered() {
    static bool once = [] {
        ProfileManager::Get().RegisterProfile(
            std::make_shared<StubProfile>("Stub Alpha (PS2)", ".stuba",
                                          std::vector<std::string>{"alpha", "sa"}));
        ProfileManager::Get().RegisterProfile(
            std::make_shared<StubProfile>("Stub Beta (PC)", ".stubb",
                                          std::vector<std::string>{"beta"}));
        return true;
    }();
    (void)once;
}

} // namespace

TEST_CASE("ProfileManager detects a profile by asking each one in turn") {
    EnsureStubsRegistered();
    ProfileManager& mgr = ProfileManager::Get();

    auto alpha = mgr.DetectProfileForFile("C:/games/disc.stuba");
    REQUIRE(alpha != nullptr);
    CHECK(alpha->GetName() == "Stub Alpha (PS2)");

    auto beta = mgr.DetectProfileForFile("/mnt/games/disc.stubb");
    REQUIRE(beta != nullptr);
    CHECK(beta->GetName() == "Stub Beta (PC)");

    CHECK(mgr.DetectProfileForFile("disc.unhandled") == nullptr);
}

TEST_CASE("ProfileManager resolves hints case-insensitively, then by name") {
    EnsureStubsRegistered();
    ProfileManager& mgr = ProfileManager::Get();

    SUBCASE("declared CLI aliases win") {
        REQUIRE(mgr.FindProfileByHint("alpha") != nullptr);
        CHECK(mgr.FindProfileByHint("alpha")->GetName() == "Stub Alpha (PS2)");
        CHECK(mgr.FindProfileByHint("SA")->GetName() == "Stub Alpha (PS2)");
        CHECK(mgr.FindProfileByHint("BeTa")->GetName() == "Stub Beta (PC)");
    }

    SUBCASE("a substring of the display name is the fallback") {
        auto found = mgr.FindProfileByHint("(pc)");
        REQUIRE(found != nullptr);
        CHECK(found->GetName() == "Stub Beta (PC)");
    }

    SUBCASE("an unknown hint resolves to nothing") {
        CHECK(mgr.FindProfileByHint("no_such_profile") == nullptr);
    }
}

TEST_CASE("ProfileManager ignores null registrations and exposes its profiles") {
    EnsureStubsRegistered();
    ProfileManager& mgr = ProfileManager::Get();
    const size_t before = mgr.GetProfiles().size();

    mgr.RegisterProfile(nullptr);
    CHECK(mgr.GetProfiles().size() == before);
    CHECK(before >= 2);

    // Every profile advertises what it can open, which is what App feeds to the
    // native file dialog.
    for (const auto& profile : mgr.GetProfiles())
        CHECK(profile->GetOpenFilter().valid());
}
