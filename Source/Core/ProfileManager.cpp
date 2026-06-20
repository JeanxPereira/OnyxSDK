#include <Onyx/Services/ProfileManager.h>

namespace Onyx::Services {

void ProfileManager::RegisterProfile(std::shared_ptr<Domain::IAssetProfile> profile) {
    if (profile) {
        m_profiles.push_back(std::move(profile));
    }
}

std::shared_ptr<Domain::IAssetProfile> ProfileManager::DetectProfileForFile(const std::filesystem::path& path) const {
    for (const auto& profile : m_profiles) {
        if (profile->Detect(path)) {
            return profile;
        }
    }
    return nullptr;
}

std::shared_ptr<Domain::IAssetProfile> ProfileManager::FindProfileByHint(const std::string& hint) const {
    std::string hintLower = hint;
    for (auto& c : hintLower) c = (char)tolower(c);

    // 1) Exact match against each profile’s declared CLI hints.
    for (const auto& profile : m_profiles) {
        for (const auto& alias : profile->GetHints()) {
            std::string a = alias;
            for (auto& c : a) c = (char)tolower(c);
            if (a == hintLower) return profile;
        }
    }

    // 2) Fallback: substring match on the profile display name.
    for (const auto& profile : m_profiles) {
        std::string nameLower = profile->GetName();
        for (auto& c : nameLower) c = (char)tolower(c);
        if (nameLower.find(hintLower) != std::string::npos) return profile;
    }
    return nullptr;
}

} // namespace Onyx::Services
