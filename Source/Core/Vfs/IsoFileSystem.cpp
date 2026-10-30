#include <Onyx/Core/Vfs/IsoFileSystem.h>

#include <Onyx/Core/Services/Log.h>
#include <Onyx/Core/Vfs/Reader.h>
#include <Onyx/Core/Vfs/SliceFile.h>

#include <algorithm>
#include <cctype>

namespace Onyx::Vfs {

namespace {

constexpr uint64_t kSector = 2048;
constexpr uint64_t kPvdOffset = 16 * kSector;
constexpr uint32_t kMinRecord = 34;
constexpr int kMaxDepth = 64;

std::string Lower(std::string_view text) {
    std::string out(text);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string StripVersion(std::string name) {
    if (const size_t semi = name.find(';'); semi != std::string::npos) name.resize(semi);
    if (name.size() > 1 && name.back() == '.') name.pop_back();
    return name;
}

std::vector<std::string> Segments(std::string_view path) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find_first_of("/\\", start);
        if (end == std::string_view::npos) end = path.size();
        if (end > start) parts.push_back(Lower(StripVersion(std::string(path.substr(start, end - start)))));
        start = end + 1;
    }
    return parts;
}

std::string Normalize(std::string_view path) {
    std::string key;
    for (const auto& part : Segments(path)) key += "/" + part;
    return key.empty() ? "/" : key;
}

std::string Join(const std::string& dir, const std::string& name) {
    return dir == "/" ? "/" + name : dir + "/" + name;
}

}

IsoFileSystem::IsoFileSystem(std::shared_ptr<const IFile> image) : m_image(std::move(image)) {}

Result<std::shared_ptr<IsoFileSystem>> IsoFileSystem::Mount(std::shared_ptr<const IFile> image) {
    using Out = Result<std::shared_ptr<IsoFileSystem>>;
    if (!image) return Out::Fail({"iso.null", "No image to mount"});
    std::shared_ptr<IsoFileSystem> fs(new IsoFileSystem(std::move(image)));
    auto loaded = fs->Load();
    if (!loaded.ok()) return Out::Fail(loaded.error());
    return Out::Ok(std::move(fs));
}

Result<void> IsoFileSystem::Load() {
    Reader reader(m_image);
    reader.Seek(kPvdOffset);
    const auto type = reader.Read<uint8_t>();
    const auto magic = reader.ReadString(5);
    if (!type || !magic || *type != 1 || *magic != "CD001") {
        return Result<void>::Fail({"iso.pvd", "Missing ISO 9660 primary volume descriptor"});
    }

    reader.Seek(kPvdOffset + 156 + 2);
    const auto extent = reader.Read<uint32_t>();
    reader.Seek(kPvdOffset + 156 + 10);
    const auto size = reader.Read<uint32_t>();
    if (!extent || !size) {
        return Result<void>::Fail({"iso.pvd", "Truncated root directory record"});
    }
    if (static_cast<uint64_t>(*extent) * kSector + *size > m_image->Size()) {
        return Result<void>::Fail({"iso.root", "Root directory extends past the end of the image"});
    }

    m_children["/"];
    std::unordered_set<uint32_t> walked;
    WalkDirectory(*extent, *size, "/", 0, walked);
    return Result<void>::Ok();
}

void IsoFileSystem::Warn(std::string code, std::string message) {
    ONYX_LOG_WARN("Iso", std::string_view(message));
    m_warnings.push_back({std::move(code), std::move(message), 0});
}

void IsoFileSystem::WalkDirectory(uint32_t extent, uint32_t size, const std::string& path, int depth,
                                  std::unordered_set<uint32_t>& walked) {
    if (depth >= kMaxDepth) {
        Warn("iso.walk.depth", "Directory nesting exceeds the depth cap at " + path);
        return;
    }
    walked.insert(extent);
    ++m_directoriesWalked;

    const uint64_t base = static_cast<uint64_t>(extent) * kSector;
    Reader reader(m_image, base, size);
    while (!reader.Eof()) {
        const uint64_t recordStart = reader.Tell();
        const auto length = reader.Read<uint8_t>();
        if (!length) break;
        if (*length == 0) {
            reader.Seek((recordStart / kSector + 1) * kSector);
            continue;
        }
        if (*length < kMinRecord) {
            Warn("iso.record.short", "Directory record shorter than 34 bytes in " + path);
            break;
        }
        const uint64_t sectorLeft = kSector - recordStart % kSector;
        if (*length > sectorLeft || recordStart + *length > size) {
            Warn("iso.record.bounds", "Directory record crosses a sector or the directory end in " + path);
            break;
        }

        reader.Seek(recordStart + 2);
        const auto recExtent = reader.Read<uint32_t>();
        reader.Seek(recordStart + 10);
        const auto recSize = reader.Read<uint32_t>();
        reader.Seek(recordStart + 25);
        const auto flags = reader.Read<uint8_t>();
        reader.Seek(recordStart + 32);
        const auto nameLen = reader.Read<uint8_t>();
        if (!recExtent || !recSize || !flags || !nameLen || 33u + *nameLen > *length) {
            Warn("iso.record.malformed", "Malformed directory record in " + path);
            break;
        }
        const auto rawName = reader.ReadString(*nameLen);
        reader.Seek(recordStart + *length);
        if (!rawName) break;

        if (*nameLen == 1 && ((*rawName)[0] == '\0' || (*rawName)[0] == '\1')) continue;
        const std::string name = StripVersion(*rawName);
        if (name.empty()) continue;

        const bool directory = (*flags & 2) != 0;
        const uint64_t offset = static_cast<uint64_t>(*recExtent) * kSector;
        if (offset + *recSize > m_image->Size()) {
            Warn("iso.entry.bounds", "Entry " + name + " extends past the end of the image");
            continue;
        }

        const std::string childPath = Join(path, Lower(name));
        if (m_entries.contains(childPath)) continue;

        if (directory && walked.contains(*recExtent)) {
            Warn("iso.walk.duplicateExtent", "Directory " + name + " reuses an extent already walked");
            continue;
        }

        Entry entry;
        entry.info = DirEntry{name, directory, *recSize};
        entry.offset = offset;
        m_entries.emplace(childPath, entry);
        m_children[path].push_back(entry.info);

        if (directory) {
            m_children[childPath];
            WalkDirectory(*recExtent, *recSize, childPath, depth + 1, walked);
        }
    }

}

std::vector<DirEntry> IsoFileSystem::List(std::string_view dir) const {
    const auto it = m_children.find(Normalize(dir));
    return it == m_children.end() ? std::vector<DirEntry>{} : it->second;
}

std::shared_ptr<IFile> IsoFileSystem::Open(std::string_view path) const {
    const auto it = m_entries.find(Normalize(path));
    if (it == m_entries.end() || it->second.info.isDirectory) return nullptr;
    return std::make_shared<SliceFile>(m_image, it->second.offset, it->second.info.size);
}

bool IsoFileSystem::Exists(std::string_view path) const {
    const std::string key = Normalize(path);
    return key == "/" || m_entries.contains(key);
}

}
