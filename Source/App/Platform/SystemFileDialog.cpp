#include <Onyx/App/UIHelpers.h>
#include <Onyx/Domain/IAssetProfile.h>
#include <cstdlib>
#include <string>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#endif

#if defined(__APPLE__)
extern "C" {
    const char* macos_openFileDialog(const char* const* extensions, int extCount);
    const char* macos_saveFileDialog(const char* defaultName);
}
#endif

std::string SystemOpenFileDialog(const std::vector<Onyx::Domain::OpenFilter>& filters) {
#ifdef _WIN32
    // Collect union of all extensions
    std::vector<std::string> allExts;
    for (auto& f : filters)
        for (auto& e : f.extensions)
            allExts.push_back(e);

    // Build \0-delimited filter buffer
    std::string buf;
    auto group = [&](const std::string& label, const std::vector<std::string>& exts) {
        std::string pat;
        for (size_t i = 0; i < exts.size(); ++i) {
            if (i) pat += ';';
            // Handle the wildcard all-files group specially
            if (exts[i] == "*")
                pat += "*.*";
            else
                pat += "*." + exts[i];
        }
        buf += label;
        buf.push_back('\0');
        buf += pat;
        buf.push_back('\0');
    };

    if (!allExts.empty())
        group("All Supported Files", allExts);
    for (auto& f : filters)
        if (f.valid())
            group(f.label, f.extensions);
    group("All Files (*.*)", {"*"});
    buf.push_back('\0'); // double-null terminator

    OPENFILENAMEA ofn = {};
    char path[MAX_PATH] = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = buf.c_str();
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameA(&ofn))
        return std::string(path);
    return "";

#elif defined(__APPLE__)
    std::vector<const char*> exts;
    for (auto& f : filters)
        for (auto& e : f.extensions)
            exts.push_back(e.c_str());
    const char* result = macos_openFileDialog(exts.empty() ? nullptr : exts.data(), (int)exts.size());
    if (result) {
        std::string p(result);
        free(const_cast<char*>(result));
        return p;
    }
    return "";
#else
    return "";
#endif
}

std::string SystemSaveFileDialog(const std::string& defaultName) {
#ifdef _WIN32
    OPENFILENAMEA ofn = {};
    char path[MAX_PATH] = {};
    strncpy(path, defaultName.c_str(), sizeof(path) - 1);
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = "All Files\0*.*\0";
    ofn.lpstrFile   = path;
    ofn.nMaxFile    = MAX_PATH;
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (GetSaveFileNameA(&ofn)) {
        return std::string(path);
    }
#elif defined(__APPLE__)
    const char* result = macos_saveFileDialog(defaultName.c_str());
    if (result) {
        std::string path(result);
        free(const_cast<char*>(result));
        return path;
    }
#endif
    return "";
}
