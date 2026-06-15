# Changelog

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
