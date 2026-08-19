#pragma once

// ── Umbrella header ───────────────────────────────────────────────────────
// Provides all UI helper functions through focused sub-headers.
// Existing #include "UIHelpers.h" call sites continue to work unchanged.

// Formatting utilities (HashHex, FormatBytes, FormatNum, MatchesFilter)
#include <Onyx/App/Formatting.h>
// Bring Onyx::App:: formatting helpers into global scope for backward compatibility
using Onyx::App::HashHex;
using Onyx::App::FormatBytes;
using Onyx::App::FormatNum;
using Onyx::App::MatchesFilter;

// TypeId → visual mapping (TypeName, ColorForType, IconForType, SkinModeName)
#include <Onyx/App/TypeVisuals.h>

// NOTE: Role → visual mapping (ColorForRole/IconForRole) is GOWR-specific and
// lives in the app (ui/RoleVisuals.h). It is deliberately NOT pulled in here —
// this umbrella is engine-side and must stay game-agnostic. App TUs that need
// role visuals include "ui/RoleVisuals.h" directly.

// -- Platform dialogs -------------------------------------------------------
#include <string>
#include <vector>
#include <Onyx/Modules/GameModule.h>

// Native open dialog filtered to the given module groups (M3b Task 6: was
// filtered by IAssetProfile::GetOpenFilter, now by IGameModule::Info()'s
// openFilters) plus an auto-added union group and an All Files group. Empty
// list: All Files.
std::string SystemOpenFileDialog(const std::vector<Onyx::Modules::OpenFilter>& filters = {});
std::string SystemSaveFileDialog(const std::string& defaultName);
