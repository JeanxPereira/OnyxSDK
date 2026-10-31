#pragma once

#include <filesystem>
#include <string>

namespace Onyx::Oracle {

// Process exit codes shared by every command.
inline constexpr int kExitWithin = 0;
inline constexpr int kExitOutside = 1;
inline constexpr int kExitMissingFile = 2;
inline constexpr int kExitMissingProfile = 3;
inline constexpr int kExitUsage = 64;
inline constexpr int kExitNoDevice = 77;

// Renders the corpus into out/<name>.png and out/<name>.json.
int RunRender(const std::filesystem::path& out);

// Renders the corpus into out and compares each PNG with the golden one under the tolerance profile.
// An empty profile picks "software" for a CPU device and "hardware" otherwise. With writeDiff each
// scene also gets out/<name>.diff.png holding four times the absolute channel difference.
int RunParity(const std::filesystem::path& golden, const std::filesystem::path& out,
              const std::filesystem::path& tolerances, const std::string& profile, bool writeDiff);

// Renders the corpus twice into out/a and out/b; succeeds when every PNG is byte identical.
int RunReproducible(const std::filesystem::path& out);

// Rewrites only the JSON reports of golden from the current renderer; refuses when a PNG is missing.
int RunBless(const std::filesystem::path& golden);

}
