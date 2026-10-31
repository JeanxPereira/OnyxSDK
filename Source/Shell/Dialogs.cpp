#include "Onyx/Shell/Dialogs.h"

namespace Onyx::Shell {

std::vector<std::string> FilterGlobs(const Modules::OpenFilter& filter) {
    std::vector<std::string> globs;
    for (const std::string& extension : filter.extensions) {
        if (extension.empty()) {
            continue;
        }
        if (extension.find('*') != std::string::npos) {
            globs.push_back(extension);
        } else if (extension.front() == '.') {
            globs.push_back("*" + extension);
        } else {
            globs.push_back("*." + extension);
        }
    }
    return globs;
}

}
