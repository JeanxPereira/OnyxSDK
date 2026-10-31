#include "Commands.h"

#include <cstdio>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace {

using namespace Onyx::Oracle;

int Usage() {
    std::fputs(
        "usage:\n"
        "  OnyxOracle render --out DIR\n"
        "  OnyxOracle parity --golden DIR --out DIR --tolerances FILE [--profile NAME] [--diff]\n"
        "  OnyxOracle reproducible --out DIR\n"
        "  OnyxOracle bless --golden DIR\n",
        stderr);
    return kExitUsage;
}

}

int main(int argc, char** argv) {
    if (argc < 2) {
        return Usage();
    }
    const std::string_view command = argv[1];
    std::map<std::string, std::string> options;
    bool diff = false;
    for (int i = 2; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--diff") {
            diff = true;
        } else if (arg.starts_with("--") && i + 1 < argc) {
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
