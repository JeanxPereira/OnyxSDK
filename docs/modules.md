# Writing a module

A module is the format knowledge Onyx does not have: how to recognise a file, how to walk its container into entries, what each entry is and how to decode it. This document walks through the module class using Prism (`Examples/Prism`), the reference module that covers every slot, then the rules for reporting problems, the tests a module ships, and how a viewer or a command attaches to a module's types. [`docs/architecture.md`](architecture.md) describes the contracts named here in the context of the whole SDK.

A module links `Onyx::Core` only. Prism's library does exactly that; its palette viewer lives in a second library, `PrismUi`, which links `Onyx::Shell`. Keeping the two apart lets the same module serve a command-line tool that has no graphics stack.

## The class

A module derives from `Modules::IGameModule`:

```cpp
class Module final : public Onyx::Modules::IGameModule {
public:
    Onyx::Modules::ModuleInfo Info() const override;
    Onyx::Modules::ProbeResult Probe(const Onyx::Modules::ProbeInput& input) const override;
    void RegisterTypes(Onyx::Types::TypeRegistrar& registrar) override;
    void RegisterDecoders(Onyx::Modules::DecoderRegistry& decoders) override;
    void RegisterSettings(Onyx::Services::SettingsSchema& schema) override;
    Onyx::Modules::ParseResult ParseContainer(Onyx::Modules::ContainerContext& ctx) override;
};
```

The host adds it with `Workspace::AddModule` before the first open. The workspace then calls `RegisterTypes`, `RegisterDecoders` and `RegisterSettings` once, and `Probe` and `ParseContainer` for each file it opens. `Mounts` is optional and covered under [Walking the container](#walking-the-container).

### Identity

`Info` returns a `ModuleInfo`: an id, a display name, hints and the open-dialog filters. Prism returns the id `prm`, the name `Prism`, the hint `prism` and an open filter for the `prm` extension. The id prefixes every type key the module registers, and the id or a hint is what a user passes as `--game <hint>` on the command line, or a host as the module hint of `Workspace::Open`, to skip probing.

### Probing

`Probe` receives a `ProbeInput`: the path, up to the first 64 KiB of the file, and the file size. It returns a `ProbeResult` with a confidence from 0 to 100 and a short reason. The workspace opens a file with the module whose confidence is highest, provided it reaches `kProbeFloor` (40) and is strictly above the runner-up. Prism returns 95 when the file starts with its `PRM1` magic and 0 otherwise. A probe should look only at the bytes it is given and must not throw; a probe that throws scores 0.

### Types

`RegisterTypes` receives a `TypeRegistrar` and adds one `TypeSpec` per kind of entry: a key without the module prefix, a label, a `Domain::MediaKind`, an icon and a colour. Prism registers six (`palette`, `image`, `manifest`, `clip`, `entries`, `stats`), which become `prm.palette` through `prm.stats` in the catalog. It keeps the returned `Types::TypeId` values as members, because the decoders and the parser refer to them. `Domain::DefaultIcon` picks the stock icon for a media kind.

### Decoders

`RegisterDecoders` binds each type to the slots it can fill. Prism's registrations, in full:

```cpp
decoders.Table(m_palette, DecodePaletteTable);
decoders.Any(m_palette, std::string(kPaletteCapability), DecodePaletteAny);
decoders.Image(m_image, DecodeImage);
decoders.Text(m_manifest, DecodeManifest);
decoders.Audio(m_clip, DecodeClip);
decoders.Table(m_entries, DecodeEntries);
decoders.Schema(m_stats, StatsSchema());
```

A decoder takes a `Modules::DecodeContext&` and returns the payload: a `std::optional` of `Domain::Texture`, `Domain::Text`, `Domain::Audio` or `Domain::Table`, a `std::unique_ptr<Domain::Scene>`, or a `std::shared_ptr<void>` for an `Any` slot. `ctx.reader` is a `Vfs::Reader` over exactly the entry's bytes; a read past its end comes back short or empty instead of reaching into the neighbouring entry. Prism's decoders are free functions. A decoder may run on a worker thread after the module object has been removed, so it must not capture a raw pointer to the module.

The same type may fill several slots. Prism's palette decodes as a `Table`, which the stock table viewer and the `decode` verb show, and as an `Any` payload for its own viewer. `DecoderRegistry::Of` reports both, and each consumer picks the slot it understands.

### Walking the container

`ParseContainer` receives a `ContainerContext` and fills `ctx.roots` with `Domain::AssetEntry` values. It reads through `ctx.reader`, which spans the whole file. Each entry gets a name, a type, a media kind and a `source` byte range: `{fileIndex, offset, size}`, where `fileIndex` indexes `ctx.files` and 0 is the opened file. Nested containers go in `AssetEntry::children`. Nothing is decoded here; the tree is built from headers, and the decoders run later, on demand.

Prism reads its header, clamps the declared section count to what the file size could hold (with a `prism.sections.clamped` warning), reads the section table, and adds one entry per section. Before adding an entry it checks what can be checked cheaply: that the range lies inside the file, that an image's declared size matches its range, that a clip's frame count matches its size. A section that fails a check is still added, flagged `NodeFlags::Failed`, and the reason is reported (see [Diagnostics](#diagnostics)). `ParseContainer` returns a `ParseResult` whose `ok` is false only when the file cannot be walked at all, such as a missing magic; one bad section does not fail the file.

A format whose files live inside another archive implements `Mounts`, returning a `MountSpec` per archive extension with a function that mounts it as an `IVirtualFileSystem`. The workspace mounts the file before parsing and passes the result as `ctx.mounted`. The parser opens the inner files through it, appends them to `ctx.files` and points entries at them by index. OnyxBox (`Examples/OnyxBox`) does this for its `.obxpak` archives.

### Settings

`RegisterSettings` declares the values a user may change, with defaults and ranges, on a `Services::SettingsSchema`:

```cpp
schema.Bool(std::string(kTransparentIndexZeroKey), false, "Make palette index 0 fully transparent");
schema.Double(std::string(kGainKey), 1.0, 0.0, 4.0, "Clip gain");
```

The keys are `prism.transparentIndexZero` and `prism.gain`; prefixing them with the module's name keeps them apart from other modules in the shared settings file. The workspace writes each default into its settings when the key is absent, the settings panel renders the schema as a form, and a decoder reads the current value through `ctx.settings`. Prism's image decoder calls `GetBool` and its clip decoder calls `GetDouble`, each with a fallback for a key that is absent or holds another type. A parse running on a worker sees the settings as they were when the open started.

### Module state

Some formats share data between entries: Prism's indexed images all decode through the one palette in the file. `ParseContainer` stores such data in `ctx.moduleState`, a `std::shared_ptr<void>` kept with the document, and every decoder of that document receives it as `DecodeContext::moduleState`. Prism parses the palette while walking and stores a `Prism::Palette`. Its image decoder casts the state back with `std::static_pointer_cast` and reports `prism.noPalette` when the file had none. The state belongs to one document, so two open files never see each other's palette.

### An Any payload

The fixed slots cover images, text, scenes, audio and tables. For anything else a module registers an `Any` decoder under a capability name and returns its own type as a `std::shared_ptr<void>`. Prism's `prism.palette` capability returns a `Prism::Palette`, declared in a public header (`Prism/Palette.h`) so that its viewer can cast the payload back. The capability name is the contract between the decoder and whatever consumes it: `Capabilities::any` lists it, the `decode` verb prints it, and a viewer names it in `ViewerSpec::anyCapability`.

### A schema record

A fixed binary record needs no decoder code. Prism describes its 16-byte stats section as a `Schema::Schema` with four fields and registers it with `DecoderRegistry::Schema`:

```cpp
auto schema = std::make_shared<Onyx::Schema::Schema>("PrismStats");
schema->Field("imageCount", Onyx::Schema::FieldType::U32);
schema->Field("clipCount", Onyx::Schema::FieldType::U32);
schema->Field("gainDefault", Onyx::Schema::FieldType::F32);
schema->Field("flags", Onyx::Schema::FieldType::U32);
```

The inspector shows the record as a form, and `decode` prints it as JSON. A schema can also hold strings, byte arrays, padding, nested structs, arrays counted by a constant or an earlier field, offsets taken from a field, and big-endian layouts. The entry's media kind stays `Raw`, so the hex view still opens it.

## Diagnostics

A module reports problems; it does not throw them.

- **Report to the sink in the context.** `ContainerContext::diags` and `DecodeContext::diags` are the document's `DiagSink`. Each report is a `Services::Diag` with a severity, a code and a message. Codes use the module's prefix (`prism.image.size`), and messages start with the entry's name, so a reader of the problems panel or of `list` output can tell which entry failed.
- **Report on the entry.** A parse-time failure flags the entry `NodeFlags::Failed` and leaves it in the tree. The browser marks it, `list` prints it with `[FAILED]`, `extract` skips it, and TestKit's decode smoke never hands it to a decoder. A decoder can still be asked for it from a viewer, so decoders check their input too.
- **Return nothing after reporting.** A decoder that cannot produce a payload reports why and returns `std::nullopt` or null. The viewer then reports `ui.open.decode` as well, but the module's own diagnostic is the one that says what was wrong.
- **Never throw.** The workspace catches what escapes `ParseContainer` and reports it as `<id>.parse-threw`, failing the open, and the registry turns a throwing decoder into `onyx.decode.threw`. Both keep the program running and lose the detail a proper report would have carried.
- **Distrust every count and offset.** Check them against `Reader::Size` or `Reader::Remaining` before allocating or seeking. Prism clamps its section count and checks every payload size before reading it.

Warnings are for data the module could work around (a clamped count); errors are for data it could not read.

## Tests

A module's tests need no binary fixtures in the repository and no GPU. Prism's suite, `Prism.Unit` in `Tests/Prism`, shows the pieces.

- **A writer.** `Prism::Writer` builds PRM1 files in memory, including deliberately broken ones: `AddRange` declares a section outside the file. `Tests/Prism/Support/PrismFixture.h` builds the sample every test opens. OnyxBox ships `OnyxBox::Writer` for the same reason. TestKit adds builders for inputs a module might sit on: `TestKit::IsoBuilder` for ISO images, `TestKit::Lz4Compress` and `TestKit::Lz4FrameCompress` for compressed payloads, and the scene fixtures in `Onyx/TestKit/SceneFixtures.h`.
- **A tree golden.** `TestKit::SnapshotTree` turns a parsed document into canonical JSON (names, type keys, kinds, sizes, flags), and `TestKit::CompareTreeGolden` compares it byte for byte with a file, writing `<golden>.actual` beside it on a mismatch. `TestKit::UpdateGolden` rewrites the golden only when `ONYX_UPDATE_GOLDENS=1` is set. Prism's tree is pinned in `Tests/Golden/prm/sample.golden.json`.
- **A decode smoke.** `TestKit::DecodeAll` runs every slot that `DecoderRegistry::Of` reports for every entry of a document and counts what decoded, what was skipped and what failed. An allowlist names the entries whose failure is expected; Prism's smoke test allowlists only its out-of-range section.
- **CLI goldens.** `Tests/Cli/PrismVerbs.cpp` runs `Cli::Run` over a `VerbTable` holding `Cli::StockVerbs`, captures the exit code, standard output and standard error, replaces the temporary path with `<file>`, and compares the text with a file in `Tests/Cli/Goldens` using the same golden functions. That pins `probe`, `list` and `decode` output byte for byte, diagnostics included.
- **Standalone headers.** Each public header of a module should compile as the only include of a translation unit. Inside the SDK, `cmake/StandaloneHeaders.cmake` generates one such file per header of a layer, but its function reads `Source/<Layer>/include` and does not apply to a module's directory. A module repeats the pattern in its own CMake:

```cmake
file(GLOB_RECURSE headers RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/include"
     "${CMAKE_CURRENT_SOURCE_DIR}/include/*.h")
foreach(header IN LISTS headers)
    string(MAKE_C_IDENTIFIER "${header}" stem)
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/standalone/${stem}.cpp"
         CONTENT "#include <${header}>\n")
    list(APPEND standalone "${CMAKE_BINARY_DIR}/standalone/${stem}.cpp")
endforeach()
add_library(MyModuleStandalone OBJECT ${standalone})
target_link_libraries(MyModuleStandalone PRIVATE MyModule)
```

The suites in this repository are registered with `onyx_add_tests` from `Tests/CMakeLists.txt`, which builds a doctest executable per layer and names the ctest entry `<Layer>.Unit`. That function is defined only when `ONYX_BUILD_TESTS` is on, which by default means Onyx is the top-level project, so a consumer's module registers its own test executable with `add_test`.

## Viewers and commands for a module's types

### A viewer

A viewer is an `IDocumentView` created from a `ViewerSpec` registered on a `ViewerRegistry`. The spec names its target as a `MediaKind` or as an exact `TypeKey`; a `TypeKey` match beats any `MediaKind` match, and ties go to the higher `priority`. When the spec names an `anyCapability`, the open decodes that `Any` slot on the job queue and the `OpenRequest` carries the result in `any`; otherwise the request carries the payload of the type's media kind. Prism's extension registers its palette viewer like this:

```cpp
Onyx::Ui::ViewerSpec spec;
spec.id = "prism.palette";
spec.target = Onyx::Types::TypeKey{"prm.palette"};
spec.priority = 10;
spec.anyCapability = "prism.palette";
spec.create = [](Onyx::Ui::OpenRequest& r) -> std::unique_ptr<Onyx::Ui::IDocumentView> {
    auto palette = std::static_pointer_cast<const Prism::Palette>(r.any);
    if (!palette) return nullptr;
    return std::make_unique<PaletteView>(r.doc, std::move(r.node), std::move(r.name), std::move(palette));
};
shell.Viewers().Register(std::move(spec));
```

In a Shell host this runs inside an `IShellExtension`, here `Prism::Ui::Extension`, which the host adds with `Shell::AddExtension`. A host with its own window calls `ViewerRegistry::Register` on its own registry with the same spec. Returning null from `create` means "no view"; the open reports `ui.open.decode` and places nothing.

### A command-line verb

The stock verbs already cover every slot: `list` shows the tree, `decode` summarises each payload the entry decodes to and names its `Any` capabilities, and `decode --to` writes through an exporter registered for the entry's media kind. A module-specific verb is a `Cli::Verb` added to the host's `VerbTable`, or to `Shell::Verbs` in a Shell host. It opens the file (`Modules::ScopedDocument` closes it again at scope exit), finds the entry with `Cli::ResolveEntry`, prints a failed lookup with `Cli::ReportResolution`, and checks the entry's type with `Workspace::Catalog().Key(entry->type)` before acting on it. `Modules::ReaderOf` gives it the entry's bytes. Paths in its arguments are UTF-8 and go through `Services::Paths::FromUtf8`.

### A UI command

A menu item or shortcut is a `Ui::Command` added to `Shell::Commands` or to the host's `CommandRegistry`. Its `enabled` predicate decides when it applies. A command that acts on the selected entry follows the `Modules::SelectionChanged` events that the document browser posts on the workspace's `EventBus`, and checks the entry's type key the same way a verb does.
