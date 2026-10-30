#include <Onyx/Core/Modules/Workspace.h>
#include <OnyxBox/Module.h>
#include <OnyxBox/Writer.h>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

void Print(const Onyx::Domain::AssetEntry& entry, int depth, size_t& count) {
    std::cout << std::string(static_cast<size_t>(depth) * 2, ' ') << entry.name << '\n';
    ++count;
    for (const auto& child : entry.children) Print(child, depth + 1, count);
}

}

int main() {
    const auto path = std::filesystem::temp_directory_path() / "onyx-coreonly-consumer.obx";
    {
        const auto bytes = OnyxBox::Writer()
                               .AddText("readme.txt", "hello")
                               .AddTable("items", {{"a", "b"}, {"c", "d"}})
                               .Build();
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }

    Onyx::Modules::Workspace workspace;
    if (!workspace.AddModule(std::make_unique<OnyxBox::Module>()).ok()) return 1;

    const auto id = workspace.Open(path);
    int rc = 1;
    if (id != 0) {
        if (const auto* doc = workspace.Get(id)) {
            size_t count = 0;
            for (const auto& root : doc->roots) Print(root, 0, count);
            if (count > 0) {
                std::cout << "Consumer ok: " << count << " entries\n";
                rc = 0;
            }
        }
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
    return rc;
}
