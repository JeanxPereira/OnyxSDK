# Changelog

## v0.5.1 — 2026-06-20

### Changed
- Third-party libs (Dear ImGui, ImPlot, ImGuiColorTextEdit) are now consumed via
  CMake FetchContent — pinned to the exact previously-vendored/submoduled commits —
  instead of a vendored copy + git submodules. No third-party source lives in the
  repo anymore; updates are a one-line `GIT_TAG` bump.
- App config migrated from the binary `GTKC` blob to human-readable **TOML**
  (`onyx.toml`) via toml++. Old `gowtool.gtkc` is not migrated (clean break: first
  run starts from defaults). Window/dock layout stays in ImGui's native `imgui.ini`.
  Removes the vestigial `AppConfig::imguiIniState` field and two GoW-named remnants
  (`gowtool.gtkc` + the `GTKC` magic).

### Added
- The default UI font is now the bundled SF Mono when the user has no saved choice.

## v0.5.0 — 2026-06-20

Decoupled God of War from the SDK — the public surface is now game-agnostic.

### Removed (breaking)
- `Onyx::Types::GameVersion` enum.
- `TypeRegistry::RegisterByMagic` / `RegisterByTag` / `ResolveByTag` and the
  `ITypeHandler::GetMagic()` method; the `REGISTER_TYPE` / `REGISTER_TAG` macros.
  The WAD magic/tag dispatch now lives in consumers (GoWToolkit's `Onyx::Gow`).
- `Onyx::Parsers::ScriptTargetParser` (GoW-specific) and the deprecated
  `Onyx::Domain::WadAssetName`.

### Changed
- `AssetVisibility` is keyed by `TypeId` alone (no game-version dimension);
  serialized filter overrides reset once on upgrade.
- `IAssetProfile::GetHints()` added — profiles declare their own CLI aliases;
  the SDK no longer hardcodes any game hints.

### Kept (the generic path)
- `TypeCatalog::Register` + `TypeRegistry::RegisterByTypeId` + `Resolve(TypeId)`
  + `REGISTER_FILE_TYPE` — unchanged.

## v0.2.0 — 2026-06-15

Generic container primitives — the reusable half of the SCUMMRedux effort's
"what's generic → Onyx" boundary (the SDK's second consumer).

### Added
- `Onyx::Container` module:
  - `ChunkReader` — config-driven IFF/RIFF 4CC chunk reader (tag size, endianness,
    alignment, header-inclusive size, size-before-tag) over a zero-copy `std::span`.
  - `ChunkSchema` — `tag → {allowed children}` registry.
  - `ChunkNode` + `BuildChunkTree` — schema-driven, tolerant chunk-tree builder
    (unknown/oversized chunks degrade to opaque leaves; never throws).
- `Onyx::Vfs::TransformFile` + `MakeXorFile` — per-byte cipher decorator on `IFile`
  (XOR; identity when key is 0).

## v0.1.0 — 2026-06-14

Initial standalone release of OnyxSDK, extracted from GoWToolkit via `git filter-repo --subdirectory-filter Engine`. Preserves commit history from the M2 engine/app split onward.

### What's included
- `Onyx` static library (`Onyx::Onyx`) with public `Include/Onyx/` headers
- All third-party dependencies wired in: Dear ImGui (docking), GLFW, GLM, lz4, glad, miniaudio, ImPlot, ImGuiColorTextEdit, FFmpeg
- `Examples/MinimalViewer` — minimal Onyx consumer app
- `Tests/` — engine-only unit tests (sanity, logger, metrics, threading, theme contrast)
- CMakePresets for debug/release Ninja builds
