#pragma once
#include <Onyx/Vfs/IVirtualFileSystem.h>
#include <Onyx/Vfs/IFile.h>
#include <Onyx/Schema/NodeInstance.h>
#include <string>
#include <memory>
#include <vector>
#include <filesystem>

#include <Onyx/Domain/Wad.h>
#include <Onyx/Types/TypeId.h>

namespace Onyx::Domain {

// Declares the files a profile can open, for the File->Open picker.
// extensions are lowercase, WITHOUT the leading dot (e.g. {"iso","wad"}).
struct OpenFilter {
    std::string label;                  // e.g. "God of War II (PS2)"
    std::vector<std::string> extensions;
    bool valid() const { return !extensions.empty(); }
};

class IAssetProfile {
public:
    virtual ~IAssetProfile() = default;

    // Nome descritivo (ex: "God of War I (PS2)")
    virtual std::string GetName() const = 0;

    // Verifica se este perfil tem capacidade de ler essa ISO ou arquivo
    virtual bool Detect(const std::filesystem::path& path) const = 0;

    // Monta uma ISO inteira em um sistema de diretórios navegável
    virtual std::shared_ptr<Vfs::IVirtualFileSystem> MountArchive(const std::filesystem::path& path) = 0;

    // Abre uma WAD/Pak do jogo e povoa a AssetContainer com o conteúdo / nós base disponíveis
    virtual bool ParseContainer(std::shared_ptr<Vfs::IFile> file, AssetContainer& outWad) = 0;

    // Dado um VFS (ex: ISO montada), o profile procura por seus arquivos base (TOC/PAK)
    // e popula a estrutura do jogo no AssetContainer
    virtual bool LoadFromArchive(std::shared_ptr<Vfs::IVirtualFileSystem> vfs, AssetContainer& outWad) = 0;

    // True if this entry represents an openable container (e.g. a WAD/archive
    // node the user can drill into). Default: false. Profiles override.
    virtual bool IsContainerEntry(const AssetEntry& entry) const { return false; }

    // Called by the database immediately before ParseContainer so a profile can
    // do any pre-parse setup (e.g. ensure an external config file exists).
    // Default: no-op. Profiles override if they need it.
    virtual void PrepareForParse(const std::filesystem::path& path) {}

    // Files this profile can open (for the File->Open picker). Default: none.
    virtual OpenFilter GetOpenFilter() const { return {}; }

    // CLI hint aliases this profile answers to (lowercase, e.g. {"gow2","ps2"}).
    // Used by ProfileManager::FindProfileByHint. Default: none.
    virtual std::vector<std::string> GetHints() const { return {}; }

};

} // namespace Onyx::Domain
