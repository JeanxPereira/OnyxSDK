#pragma once

#include <Onyx/Core/Modules/GameModule.h>
#include <Onyx/Core/Types/TypeId.h>

namespace Prism {

// Module id "prm". The container format is described in Prism/Format.h.
class Module final : public Onyx::Modules::IGameModule {
public:
    Onyx::Modules::ModuleInfo Info() const override;
    Onyx::Modules::ProbeResult Probe(const Onyx::Modules::ProbeInput& input) const override;
    void RegisterTypes(Onyx::Types::TypeRegistrar& registrar) override;
    void RegisterDecoders(Onyx::Modules::DecoderRegistry& decoders) override;
    void RegisterSettings(Onyx::Services::SettingsSchema& schema) override;
    Onyx::Modules::ParseResult ParseContainer(Onyx::Modules::ContainerContext& ctx) override;

private:
    Onyx::Types::TypeId m_palette;
    Onyx::Types::TypeId m_image;
    Onyx::Types::TypeId m_manifest;
    Onyx::Types::TypeId m_clip;
    Onyx::Types::TypeId m_entries;
    Onyx::Types::TypeId m_stats;
};

}
