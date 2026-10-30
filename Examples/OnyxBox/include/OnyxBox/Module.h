#pragma once

#include <Onyx/Core/Domain/AssetEntry.h>
#include <Onyx/Core/Modules/GameModule.h>
#include <Onyx/Core/Services/Diagnostics.h>
#include <Onyx/Core/Types/TypeId.h>
#include <Onyx/Core/Vfs/Reader.h>

#include <cstdint>
#include <vector>

namespace OnyxBox {

// Module id "obx". The container formats are described in OnyxBox/Format.h.
class Module final : public Onyx::Modules::IGameModule {
public:
    Onyx::Modules::ModuleInfo Info() const override;
    Onyx::Modules::ProbeResult Probe(const Onyx::Modules::ProbeInput& input) const override;
    void RegisterTypes(Onyx::Types::TypeRegistrar& registrar) override;
    void RegisterDecoders(Onyx::Modules::DecoderRegistry& decoders) override;
    void RegisterSettings(Onyx::Services::SettingsSchema& schema) override;
    std::vector<Onyx::Modules::MountSpec> Mounts() const override;
    Onyx::Modules::ParseResult ParseContainer(Onyx::Modules::ContainerContext& ctx) override;

private:
    enum class Walk { Clean, Salvaged, Bad };

    Walk WalkToc(Onyx::Vfs::Reader& reader, uint32_t fileIndex, Onyx::Services::DiagSink& diags,
                 std::vector<Onyx::Domain::AssetEntry>& out) const;
    void Classify(uint8_t kind, Onyx::Domain::AssetEntry& entry) const;

    Onyx::Types::TypeId m_blob;
    Onyx::Types::TypeId m_image;
    Onyx::Types::TypeId m_text;
    Onyx::Types::TypeId m_mesh;
    Onyx::Types::TypeId m_table;
};

}
