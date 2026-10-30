#pragma once

#include <Onyx/Core/Domain/AssetEntry.h>
#include <Onyx/Core/Services/Diagnostics.h>
#include <Onyx/Core/Services/Jobs.h>
#include <Onyx/Core/Services/Settings.h>
#include <Onyx/Core/Vfs/Reader.h>

#include <memory>

namespace Onyx::Modules {

struct Document;

struct DecodeContext {
    Document* doc = nullptr;
    const Domain::AssetEntry& entry;
    Vfs::Reader reader;
    Services::DiagSink& diags;
    Services::Progress& progress;
    Services::Settings& settings;
    std::shared_ptr<void>& moduleState;
};

}
