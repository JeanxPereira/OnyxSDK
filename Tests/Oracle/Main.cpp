#include "Commands.h"

#include <cstdio>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <string_view>

namespace {

using namespace Onyx::Oracle;

constexpr const char* kUsage =
    "usage:\n"
    "  OnyxOracle render --out DIR\n"
    "  OnyxOracle parity --golden DIR --out DIR --tolerances FILE [--profile NAME] [--diff]\n"
    "  OnyxOracle reproducible --out DIR\n"
    "  OnyxOracle bless --golden DIR\n"
    "  OnyxOracle --help\n";

constexpr const char* kExitCodes =
    "exit codes:\n"
    "  0   success; every scene within tolerance\n"
    "  1   a scene is outside tolerance, or a render or write failed\n"
    "  2   a golden image or the tolerances file is missing\n"
    "  3   the tolerance profile is missing from the tolerances file\n"
    "  64  usage error, including --out equal to the golden directory\n"
    "  77  no Vulkan device available\n";

int Usage() {
    std::fputs(kUsage, stderr);
    return kExitUsage;
}

}

int main(int argc, char** argv) {
    if (argc < 2) {
        return Usage();
    }
    const std::string_view command = argv[1];
    if (command == "--help" || command == "-h") {
        std::fputs(kUsage, stdout);
        std::fputs(kExitCodes, stdout);
        return kExitWithin;
    }
    std::set<std::string_view> allowed;
    if (command == "render" || command == "reproducible") {
        allowed = {"out"};
    } else if (command == "parity") {
        allowed = {"golden", "out", "tolerances", "profile", "diff"};
    } else if (command == "bless") {
        allowed = {"golden"};
    } else {
        return Usage();
    }
    std::map<std::string, std::string> options;
    bool diff = false;
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (!arg.starts_with("--") || !allowed.contains(arg.substr(2))) {
            std::fprintf(stderr, "unknown argument for %s: %s\n", argv[1], argv[i]);
            return Usage();
        }
        if (arg == "--diff") {
            diff = true;
        } else if (i + 1 < argc) {
            options[std::string(arg.substr(2))] = argv[++i];
        } else {
            return Usage();
        }
    }
    auto need = [&](const char* key, std::filesystem::path& into) {
        auto it = options.find(key);
        if (it == options.end()) {
            return false;
        }
        into = it->second;
        return true;
    };

    std::filesystem::path golden;
    std::filesystem::path out;
    std::filesystem::path tolerances;
    if (command == "render") {
        return need("out", out) ? RunRender(out) : Usage();
    }
    if (command == "parity") {
        if (!need("golden", golden) || !need("out", out) || !need("tolerances", tolerances)) {
            return Usage();
        }
        const auto profile = options.find("profile");
        return RunParity(golden, out, tolerances, profile == options.end() ? std::string() : profile->second, diff);
    }
    if (command == "reproducible") {
        return need("out", out) ? RunReproducible(out) : Usage();
    }
    if (command == "bless") {
        return need("golden", golden) ? RunBless(golden) : Usage();
    }
    return Usage();
}
