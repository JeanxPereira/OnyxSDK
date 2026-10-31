# Onyx v2 — W3 Ui + Shell Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver `Onyx::Ui` (ImGui components a host draws inside its own frame: contracts, theme with one writer, widgets, icons, texture pool, stock panels and viewers, the Viewport) and `Onyx::Shell` (the ready-made application: window, present, backend, dock, menu bar from commands, recents, settings, lifecycle, dialogs), with `Examples/MinimalViewer` opening OnyxBox files and `Examples/Embedded` drawing Onyx panels inside a host's own GLFW window, ImGui context and adopted Vulkan device.

**Architecture:** `Ui` never creates an `ImGuiContext` and never includes GLFW; it draws into the current context through `UiContext`, binds images through an `ITextureBinder`, and renders through `ImGuiVulkanBackend` (the ImGui Vulkan backend over `Gpu::Device`, dynamic rendering). Every stock panel is an `IPanel` with an id; viewers are `IDocumentView`s created by a `ViewerRegistry` routed by `MediaKind`/`TypeKey`; the menu bar is rendered from a `CommandRegistry`. `Theme` is the only writer of `ImGuiStyle` (v1 Appearance ported as an object). `Shell` composes `NativeWindow` (GLFW 3.4), `Gpu::Device`/`Swapchain`/`FrameRing`, `ImGuiVulkanBackend`, `Dock`, `MenuBar`, `Recents`, `Settings`, `Dialogs`, `LifecycleDelegate`; no part exceeds ~300 lines; no global state.

**Tech Stack:** C++20, Dear ImGui docking at the v1 pin `2af6dd9694288e6befe1edb7ce25510911693c22` (core + `imgui_impl_vulkan` in Ui, `imgui_impl_glfw` in Shell), GLFW `3.4`, the existing Gpu/Render/Core layers, stb (fonts via ImGui's stb_truetype), toml++ (through `Services::Settings`), doctest. No ImPlot, no ImGuiColorTextEdit (v1 extras dropped: the spec §3 dependency list does not carry them).

**Spec:** `docs/superpowers/specs/2026-10-30-onyx-v2-design.md` (§4 layers and ImGui ownership, §8 Ui, §9 Shell, §14 examples, §15 repository, §16 W3 gate, §17 builder choices). Survey: `docs/design/2026-10-30-survey/survey-app.md`. Theme design to keep: `docs/design/appearance-system.md`. v1 reference (read-only, never included): `legacy/Source/App`, `legacy/Source/Viewers`, `legacy/Source/Services/{Appearance,ThemeManager,FrameScheduler,RecentFiles}.cpp`, `legacy/Source/Fonts`, `legacy/Include/Onyx/{App,Viewers,Services,Fonts}`.

## Global Constraints

- Branch `v2`. Commit per task with Conventional Commits, imperative Sentence-case subject **of at most 72 characters**, explicit paths in `git add`, **no attribution line of any kind**. Never push.
- The verdict is `py -3 tools/check.py --preset windows-debug --jobs 4`. Use `--ctest-regex` while iterating. Never run `check.py` in a tree where another implementer is building.
- Public headers only under `Source/Ui/include/Onyx/Ui/` and `Source/Shell/include/Onyx/Shell/`; namespaces `Onyx::Ui`, `Onyx::Shell`. Every public header compiles standalone. Layer edges only down: `Ui` links `Onyx::Render` (→ Gpu → Core) plus `imgui::imgui`; `Shell` links `Onyx::Ui` plus `imgui::glfw` and `glfw`. Lint forbids in Ui: `GLFW/`, `libav`, `miniaudio`; in Shell: `libav`. `Onyx::Ui` never includes `<GLFW/glfw3.h>`; `Onyx::Core`/`Gpu`/`Render` never include `imgui.h`.
- No `static X& Get()` / `Instance()` singletons and no process-wide mutable state: every object reaches its `UiContext`, `Theme`, `TexturePool`, `Workspace` by reference. The only process-wide object remains `Log::Default()`. ImGui's own global context is the host's (or the Shell's); `Ui` never calls `ImGui::CreateContext`.
- One writer of `ImGuiStyle`: `Theme::Commit`. No panel, widget or viewer assigns `ImGui::GetStyle()` or `style.Colors[...]` (lint rule added in Task 1: the tokens `GetStyle().Colors[` and `style.Colors[` are forbidden under `Source/Ui` and `Source/Shell` outside `Source/Ui/Theme.cpp`).
- Every `Result`/`VkResult` checked; errors through `Onyx::Result`. Codes used here: `ui.texture.format`, `ui.texture.size`, `ui.backend.init`, `ui.font.missing`, `ui.viewer.none`, `ui.command.unknown`, `shell.window`, `shell.surface`, `shell.dialog`, `shell.args`.
- No game name (`gow`, `gowr`, `gow1`, `gow2`, `ragnarok`, `wad`, `vag`, `vpk`, `pss`) under `Source/`, `Examples/`, `Tests/`; no console name; no comment narrates a task, milestone, fix round, review or previous version. English only. PascalCase files and types.
- Tests use doctest. Suites: `Ui.Unit` (headless ImGui context created by the test executable, no GPU), `Ui.Gpu` (needs a Vulkan device, SKIP 77), `Shell.Unit` (no window), `Shell.Window` (needs GLFW + a device; SKIP 77 when `glfwInit` or the device fails), `Examples.Build` (the two examples compile and `MinimalViewer --help` runs). GPU suites require the validation layer unless `ONYX_NO_VALIDATION=1` and end every case with `ValidationMessageCount() == 0`.
- The machine: AMD RX 6750 XT with validation layers; use `--jobs 4`; the Agent `isolation: worktree` branches from `main`, so lanes are hand-made from `v2`.

## Review Focus

1. A host that creates its own `ImGuiContext` and ImGui Vulkan backend: `Ui` must draw into it without creating a second context, and `TexturePool` must bind images through the host's `ITextureBinder` (Task 4, Task 10 Embedded).
2. A document whose root has 20 000 flat children: `DocumentBrowser` draws clipped and allocates no per-frame strings per row (Task 6).
3. A panel that mutates theme state every frame (a colour slider drag): `Theme::Commit` rebuilds the palette only when the state changed and never rebakes the atlas for a scale change (Task 3).
4. The window close button with unsaved work: `LifecycleDelegate::WillClose` returning false keeps the window open and `Run` does not return (Task 9).
5. A `.obx` dropped on the window or passed as `argv[1]`: the Shell opens it through the workspace, the browser shows it, double-click on an image entry opens the Image viewer (Task 9, Task 10).

---

## Dependency map and lanes

```
Wave A:            T1 Ui/Shell deps, imgui targets, icon table, assets helper, test rigs [main tree]
Wave B (parallel): T2 contracts [lane] | T3 Theme + fonts [lane] | T4 TexturePool + Vulkan backend [lane] | T5 widgets [lane]
Wave C (parallel): T6 workspace panels [lane] | T7 viewers + opening [lane] | T8 Viewport, Transport, Timeline [lane]
Wave D:            T9 Shell window, present, backend, loop [main tree]
Wave E:            T10 Shell composition: dock, menu bar, recents, lifecycle, dialogs, Run [main tree]
Wave F (parallel): T11 MinimalViewer + Embedded [lane] | T12 CI + docs [lane]
```

Sources and tests are globbed; lanes never edit the same CMake file. T1 is the only task that edits `cmake/`, `Tests/CMakeLists.txt`, `Source/Ui/CMakeLists.txt`, `Source/Shell/CMakeLists.txt`.

---

## Task 1: Ui and Shell dependencies, icon table, assets helper, test rigs

**Files:**
- Create: `cmake/Deps/Imgui.cmake`, `cmake/Deps/Glfw.cmake`, `cmake/Assets.cmake`, `cmake/GenerateIconTable.cmake`
- Modify: `Source/Ui/CMakeLists.txt`, `Source/Shell/CMakeLists.txt`, `Tests/CMakeLists.txt` (`add_subdirectory(Ui)`, `add_subdirectory(Shell)` under the option guards), `tools/lint.py` (style-writer rule), `tools/lint_test.py`
- Create: `Source/Ui/Pch.h`, `Source/Shell/Pch.h`, `Source/Ui/include/Onyx/Ui/SfSymbols.h` (the v1 `ICON_SF_*` defines, ported verbatim from `legacy/Include/Onyx/Fonts/SFSymbols.h`), `Source/Ui/include/Onyx/Ui/Icons.h`, `Source/Ui/Icons.cpp` (uses the generated table)
- Create: `Tests/Ui/CMakeLists.txt`, `Tests/Ui/Main.cpp` (headless ImGui main), `Tests/Ui/Support/HeadlessImGui.h`, `Tests/Ui/Gpu/Main.cpp` (probe main), `Tests/Shell/CMakeLists.txt`, `Tests/Shell/Main.cpp`, `Tests/Shell/Window/Main.cpp` (probe main: `glfwInit` with `GLFW_PLATFORM` default and `Device::Create` → 77 on failure)

**Interfaces:**
- Produces: `onyx_dep_imgui()`: if `TARGET imgui::imgui` already exists, reuse it (the embedding-host rule); otherwise FetchContent `ocornut/imgui` at `2af6dd9694288e6befe1edb7ce25510911693c22` and define `Onyx_imgui` STATIC (core sources + `backends/imgui_impl_vulkan.cpp`) with PUBLIC definitions `IMGUI_IMPL_VULKAN_USE_VOLK`, `IMGUI_DEFINE_MATH_OPERATORS`, `IMGUI_DISABLE_OBSOLETE_FUNCTIONS`, PUBLIC include of the imgui root and `backends/`, linking `volk::volk` and `Vulkan::Headers`; alias `imgui::imgui`. Also `Onyx_imgui_glfw` STATIC (`backends/imgui_impl_glfw.cpp`, links `imgui::imgui glfw`) with alias `imgui::glfw`, defined only after `onyx_dep_glfw()`.
- Produces: `onyx_dep_glfw()`: FetchContent `glfw/glfw` tag `3.4` with `GLFW_BUILD_DOCS/TESTS/EXAMPLES OFF`, `GLFW_INSTALL OFF`; target `glfw`.
- Produces: `onyx_install_assets(<target>)` in `cmake/Assets.cmake`: a post-build copy of `third_party/fonts/SFMono-Regular.otf` and `SFSymbols.ttf` into `$<TARGET_FILE_DIR:target>/assets/fonts/`; `Ui::AssetPaths` (Task 3) looks there first.
- Produces: `cmake/GenerateIconTable.cmake` (port of the v1 script) generating `${PROJECT_BINARY_DIR}/generated/Onyx/Ui/IconTable.cpp` from `SfSymbols.h` at configure time; `Onyx/Ui/Icons.h` declares `struct IconEntry { const char* name; const char* glyph; }; std::span<const IconEntry> IconTable(); const char* Icon(IconId)` mapping the Core `Types::IconId` enum to glyphs (the v1 `TypeVisuals` mapping ported).
- Produces: `Source/Ui/CMakeLists.txt`: `onyx_dep_imgui()`, `onyx_add_layer(Ui SOURCES ${generated IconTable.cpp} PUBLIC_DEPS Onyx::Render imgui::imgui)`; `Source/Shell/CMakeLists.txt`: `onyx_dep_glfw()`, `onyx_dep_imgui()` (for `imgui::glfw`), `onyx_add_layer(Shell PUBLIC_DEPS Onyx::Ui imgui::glfw glfw)`.
- Produces: `Tests/Ui/Support/HeadlessImGui.h`: `struct HeadlessImGui { HeadlessImGui(); ~HeadlessImGui(); void NewFrame(float dt = 1.0f/60); void EndFrame(); ImGuiContext* ctx; }` — creates a context, sets `io.DisplaySize = {1280, 720}`, adds the default font, `io.IniFilename = nullptr`, `io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures` with a tiny software texture updater that marks `ImTextureData` as `ImTextureStatus_OK` (so 1.92's on-demand font rasterisation works without a GPU), and destroys the context. `Tests/Ui/CMakeLists.txt`: `onyx_add_tests(Ui LINK Onyx::TestKit Onyx::ExampleBox)` and `onyx_add_tests(Ui SUITE Gpu SKIP_RETURN_CODE 77 LINK Onyx::TestKit Onyx::ExampleBox)`; `Tests/Shell/CMakeLists.txt`: `onyx_add_tests(Shell LINK Onyx::TestKit Onyx::ExampleBox)` and `onyx_add_tests(Shell SUITE Window SKIP_RETURN_CODE 77 LINK Onyx::TestKit Onyx::ExampleBox)`.
- Produces: lint rule (f): under `Source/Ui` and `Source/Shell`, the tokens `GetStyle().Colors[` and `style.Colors[` are forbidden except in `Source/Ui/Theme.cpp`; `lint_test.py` gains the case (`ok 6`).

- [ ] **Step 1: Write the four cmake files and the two layer CMakeLists.** Verify `imgui::imgui` reuse by configuring a scratch dir that pre-defines an `imgui::imgui` INTERFACE target before `add_subdirectory` of Onyx is impractical here; instead add the reuse branch and a `message(STATUS "Onyx: using the host's imgui::imgui")` line, and leave the proof to Task 11's Embedded example.
- [ ] **Step 2: Port `SfSymbols.h`** verbatim into `Source/Ui/include/Onyx/Ui/SfSymbols.h` (keep the `ICON_SF_*` macro names; add `#pragma once`), write `Icons.h/.cpp` and the generator.
- [ ] **Step 3: Write the test rigs** with one rig test per suite: `Ui.Unit` — `TEST_CASE("Headless ImGui renders a frame")` creating `HeadlessImGui`, `NewFrame`, `ImGui::Begin("x")/End`, `EndFrame`, `CHECK(ImGui::GetDrawData()->CmdListsCount >= 1)`; `Ui.Gpu`, `Shell.Unit`, `Shell.Window` — rig placeholders.
- [ ] **Step 4: Verify** `py -3 tools/check.py --preset windows-debug --jobs 4 --ctest-regex "Ui|Shell|Consumer"`: `_deps` gains `imgui` and `glfw`; `Consumer.CoreOnly` still passes (its forbidden list already names `imgui glfw`); `py -3 tools/lint_test.py` prints `ok 6`.
- [ ] **Step 5: Commit** `build(ui): add imgui and glfw targets, icon table, assets helper, rigs`

## Task 2: Ui contracts — UiContext, IPanel, CommandRegistry, ViewerRegistry, FrameClock, PanelHost

**Files:**
- Create: `Source/Ui/include/Onyx/Ui/{UiContext,Panel,Command,Shortcut,ViewerRegistry,FrameClock,PanelHost}.h`, `Source/Ui/{Command,Shortcut,ViewerRegistry,FrameClock,PanelHost}.cpp`
- Test: `Tests/Ui/{Command,Shortcut,ViewerRegistry,FrameClock,PanelHost}.cpp`

**Interfaces:**
- Consumes: `Modules::Workspace`, `DocumentId`, `NodePath`, `Types::TypeCatalog`, `TypeId`, `TypeKey` (`std::string`), `Domain::MediaKind`, `DecoderRegistry` + `Capabilities Of(TypeId)`, `Gpu::Device`, `Gpu::Frame`.
- Produces (`UiContext.h`):
  ```cpp
  namespace Onyx::Ui {
  class TexturePool; class Theme; class CommandRegistry; class ViewerRegistry; class PanelHost; class FrameClock;
  struct UiContext {
      Modules::Workspace& workspace;
      Gpu::Device* gpu;          // null when the host has no GPU layer
      Gpu::Frame* frame;         // the host's current frame; null without GPU
      TexturePool& textures;
      Theme& theme;
      CommandRegistry& commands;
      ViewerRegistry& viewers;
      PanelHost& panels;
      FrameClock& clock;
      float dt;
  };
  }
  ```
- Produces (`Panel.h`): `class IPanel { virtual ~IPanel(); virtual std::string_view Id() const = 0; virtual std::string_view Title() const = 0; virtual void Draw(UiContext&) = 0; bool visible = true; };` `class IDocumentView : public IPanel { virtual Modules::DocumentId Document() const = 0; virtual Modules::NodePath Node() const = 0; };`
- Produces (`Shortcut.h`): `struct Shortcut { ImGuiKeyChord chord = 0; };` `std::optional<Shortcut> ParseShortcut(std::string_view text)` accepting `Ctrl+Shift+O`, `Alt+F4`, `F1`, `Ctrl+,`, `Ctrl+Plus` (modifier names `Ctrl`, `Shift`, `Alt`, `Super`, case-insensitive, `+` separator, key names as ImGui's `ImGui::GetKeyName` minus the `Key` prefix); `std::string FormatShortcut(Shortcut)` producing the canonical `Ctrl+Shift+O` text; `bool ShortcutPressed(Shortcut)` (`ImGui::IsKeyChordPressed` with `ImGuiInputFlags_RouteGlobal`).
- Produces (`Command.h`): `struct Command { std::string id; std::string label; std::string menuPath; std::optional<Shortcut> shortcut; std::function<bool(UiContext&)> enabled; std::function<void(UiContext&)> run; };` `class CommandRegistry { void Add(Command); void Remove(std::string_view id); void Override(std::string_view id, Command); Result<void> Run(std::string_view id, UiContext&); const std::vector<Command>& All() const; const Command* Find(std::string_view id) const; void PollShortcuts(UiContext&); }` — `Run` returns `ui.command.unknown` for an unknown id and does nothing when `enabled` returns false; `PollShortcuts` runs the first enabled command whose chord is pressed; `Override` replaces fields of an existing command keeping its id (error if missing).
- Produces (`ViewerRegistry.h`):
  ```cpp
  struct OpenRequest { Modules::Workspace& workspace; Modules::DocumentId doc; Modules::NodePath node; Types::TypeId type; Domain::MediaKind kind; std::string name;
                       std::optional<Domain::Texture> image; std::optional<Domain::Text> text; std::unique_ptr<Domain::Scene> scene; std::optional<Domain::Audio> audio; std::optional<Domain::Table> table; std::vector<std::byte> raw; };
  struct ViewerSpec { std::string id; std::variant<Domain::MediaKind, Types::TypeKey> target; int priority = 0; std::function<std::unique_ptr<IDocumentView>(OpenRequest&)> create; };
  class ViewerRegistry { void Register(ViewerSpec); void Unregister(std::string_view id); const ViewerSpec* Route(const Types::TypeCatalog&, const Modules::DecoderRegistry&, Types::TypeId) const; std::span<const ViewerSpec> All() const; };
  ```
  `Route`: a `TypeKey` target matching the type's key wins over any `MediaKind` target; among equals the higher `priority`, then the earlier registration; a `MediaKind` target applies only when `DecoderRegistry::Of(type)` offers the matching slot (Image → `image`, Text → `text`, Mesh → `scene`, Audio → `audio`, Table → `table`) or the kind is `Raw`/`Unknown` (hex needs no decoder); returns null when nothing applies.
- Produces (`FrameClock.h`): `class FrameClock { void RequestRedraw(); void RequestAnimation(double seconds); bool BeginFrame(double nowSeconds); bool WantsContinuous() const; double RemainingAnimation() const; }` ported from `legacy/Source/Services/FrameScheduler.cpp` as an object.
- Produces (`PanelHost.h`): `class PanelHost { void Add(std::unique_ptr<IPanel>); void Remove(std::string_view id); IPanel* Find(std::string_view id); std::span<const std::unique_ptr<IPanel>> All() const; void DrawAll(UiContext&); }` — `DrawAll` wraps each visible panel in `ImGui::Begin(Title##Id, &visible)` / `End`, skipping panels whose `Draw` throws (caught, logged once per panel id through `Log::Default()`, panel hidden).

- [ ] **Step 1: Write the failing tests** (headless): `ParseShortcut("Ctrl+Shift+O")` chord equals `ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_O` and round-trips through `FormatShortcut`; bad text → nullopt; `CommandRegistry` Add/Run/Remove/Override and `ui.command.unknown`; disabled command not run; `ViewerRegistry::Route` prefers TypeKey over MediaKind, priority ties, and returns null for an Image type whose decoder registry has no image slot (build a `TypeCatalog` + `DecoderRegistry` by hand); `FrameClock` continuous while an animation remains; `PanelHost` draws visible panels only and hides a throwing panel after one frame.
- [ ] **Step 2: Run** `OnyxUiTests` → FAIL. **Implement.** Run → PASS.
- [ ] **Step 3: Verify** `check.py --jobs 4 --ctest-regex "Ui.Unit"` green; `OnyxUiStandalone` compiles the new headers.
- [ ] **Step 4: Commit** `feat(ui): contracts, command and viewer registries, frame clock, panel host`

## Task 3: Theme — one writer — and fonts

**Files:**
- Create: `Source/Ui/include/Onyx/Ui/{Theme,ThemeState,Fonts,AssetPaths}.h`, `Source/Ui/{Theme,ThemeResolve,ThemePalette,Fonts,AssetPaths}.cpp`
- Test: `Tests/Ui/{ThemeResolve,ThemePersist,Fonts}.cpp`

**Interfaces:**
- Consumes: `Services::Settings` (`Get/Set/Has`), `Services::SettingsSchema`, `Paths::Executable`.
- Produces (`ThemeState.h`): `enum class ThemeMode : uint8_t { Dark, Light, System };` `struct ColorOverride { std::string token; glm::vec4 color; };` (token = the ImGui colour name without `ImGuiCol_`, e.g. `"TabHovered"`; persisted by name) `struct ThemeState { std::string fontPath; float fontSizePt = 15.0f; float userScale = 1.0f; glm::vec4 accent{0.282f, 0.173f, 0.398f, 1.0f}; ThemeMode mode = ThemeMode::System; std::vector<ColorOverride> overrides; bool operator==(const ThemeState&) const = default; };` `struct ThemeEnvironment { float nativeScale = 1.0f; bool systemPrefersDark = true; std::string defaultFontPath; };` `struct ThemeResolved { float globalScale, textBasePx, fontScaleMain, fontScaleDpi; ThemeMode effectiveMode; ImGuiStyle style; std::string atlasFontPath; float atlasRefPx; };` `ImGuiStyle HouseStyle(); ThemeResolved Resolve(const ThemeState&, const ThemeEnvironment&);` (pure, ported from `legacy/Source/Services/Appearance.cpp` and `ThemeManager.cpp`'s `BuildPalette` with the contrast helpers `TextForSurface`, `ContrastColor`) `std::optional<int> ColorTokenIndex(std::string_view token); std::string_view ColorToken(int imguiCol);`
- Produces (`Theme.h`):
  ```cpp
  class Theme {
  public:
      explicit Theme(Services::Settings& settings);                // reads ui.theme.* keys; missing keys → defaults
      const ThemeState& State() const; const ThemeEnvironment& Env() const; const ThemeResolved& Applied() const;
      void Mutate(const std::function<void(ThemeState&)>&, bool animateColors = false);
      void Set(const ThemeState&); void SetEnvironment(const ThemeEnvironment&);
      void Commit();              // ONCE per frame, outside NewFrame..Render: resolves, assigns ImGui::GetStyle(), marks the atlas dirty
      void Update(float dt);      // colour transition (0.25 s ease-out); calls Commit when a step applies
      bool AtlasDirty() const; void ClearAtlasDirty();   // the backend rebuilds the font texture when dirty
      bool IsTransitioning() const;
      void Save();                // writes ui.theme.* into the Settings (not to disk)
      static void DescribeSettings(Services::SettingsSchema&);   // Enum mode, Double fontSizePt [8,72], Double userScale [0.5,3], String fontPath, String accent "#rrggbb"
  };
  ```
  Settings keys: `ui.theme.mode` (enum `dark|light|system`), `ui.theme.fontSizePt`, `ui.theme.userScale`, `ui.theme.fontPath`, `ui.theme.accent` (`#rrggbbaa`), `ui.theme.overrides` (string: `Token=#rrggbbaa;Token2=...`).
- Produces (`Fonts.h`): `struct FontSet { ImFont* text = nullptr; ImFont* mono = nullptr; ImFont* icons = nullptr; };` `Result<FontSet> BuildFonts(ImFontAtlas&, const std::filesystem::path& textFont, float refPx, const std::filesystem::path& iconFont)` — clears the atlas, adds the text font, merges the SF Symbols range (`0xE000..0xF8FF`) with the v1 glyph offset/advance settings; `ui.font.missing` when a file is absent.
- Produces (`AssetPaths.h`): `std::filesystem::path FindAsset(std::string_view relative)` — looks in `<exe dir>/assets/<relative>`, then `<exe dir>/<relative>`, then `third_party/fonts/<name>` relative to `ONYX_SOURCE_DIR` when the compile definition exists (tests only); returns empty when not found.

- [ ] **Step 1: Write the failing tests** (`Ui.Unit`, headless): the six from `docs/design/appearance-system.md` ("Tests" section: determinism, idempotence, independence, scaling law, font resolution to the bundled SF Mono, persistence round-trip `ThemeState → Settings → ThemeState` identity including overrides by token); `Commit` with no change assigns nothing (compare `ImGui::GetStyle()` bytes before/after); `Mutate` of `userScale` leaves `AtlasDirty()` false, of `fontSizePt` sets it; `ColorTokenIndex("TabHovered") == ImGuiCol_TabHovered` and an unknown token → nullopt; `BuildFonts` with the real `third_party/fonts` files yields three non-null fonts and `ui.font.missing` for a bad path.
- [ ] **Step 2: Run** → FAIL. **Implement**, porting `Resolve`/`HouseStyle`/palette/transition from v1 into the object (no statics; the transition state lives in `Theme`).
- [ ] **Step 3: Run** → PASS. **Verify** `check.py --jobs 4 --ctest-regex "Ui.Unit"`; `tools/lint.py` passes with the new style-writer rule (only `Theme.cpp` assigns the style).
- [ ] **Step 4: Commit** `feat(ui): theme with one writer, token overrides, fonts`

## Task 4: TexturePool, ITextureBinder and the ImGui Vulkan backend

**Files:**
- Create: `Source/Ui/include/Onyx/Ui/{TexturePool,TextureBinder,ImGuiVulkanBackend}.h`, `Source/Ui/{TexturePool,ImGuiVulkanBackend}.cpp`
- Test: `Tests/Ui/Gpu/{TexturePool,Backend}.cpp`, `Tests/Ui/Gpu/Support/UiGpuFixture.h`

**Interfaces:**
- Consumes: `Gpu::Device`, `UploadContext`, `Image`, `RetireQueue`, `RenderTarget`, `Frame`, `FrameRing`, `Domain::Texture`, `ReadbackColor`, Task 3 `AtlasDirty`.
- Produces (`TextureBinder.h`): `class ITextureBinder { virtual ~ITextureBinder(); virtual Result<ImTextureID> Bind(VkImageView view, VkImageLayout layout, VkSampler sampler) = 0; virtual void Unbind(ImTextureID) = 0; };`
- Produces (`TexturePool.h`):
  ```cpp
  class TexturePool {
  public:
      TexturePool(Gpu::Device&, Gpu::UploadContext&, ITextureBinder&);
      Result<ImTextureID> Create(const Domain::Texture&);                       // uploads (RGBA8/BC/F16 as UploadContext supports), binds; ui.texture.size / ui.texture.format on failure
      Result<void> Update(ImTextureID, const Domain::Texture&);                // same size/format: re-upload; else ui.texture.size
      Result<ImTextureID> Register(VkImageView view, VkImageLayout layout);     // an external view (the Viewport's resolve); not owned
      void Remove(ImTextureID);                                                // unbinds now, retires the image through Gpu::RetireQueue
      glm::uvec2 Size(ImTextureID) const; size_t Count() const;
      Gpu::FenceValue Flush();                                                   // flushes pending uploads; the host waits/orders through the frame
  };
  ```
- Produces (`ImGuiVulkanBackend.h`):
  ```cpp
  struct BackendOptions { VkFormat colorFormat = VK_FORMAT_B8G8R8A8_UNORM; VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT; uint32_t imageCount = 2; };
  class ImGuiVulkanBackend : public ITextureBinder {
  public:
      static Result<std::unique_ptr<ImGuiVulkanBackend>> Create(Gpu::Device&, const BackendOptions&);   // ImGui_ImplVulkan_Init with dynamic rendering, its own descriptor pool and sampler; ui.backend.init on failure
      ~ImGuiVulkanBackend();                                                  // waits the queue timeline then ImGui_ImplVulkan_Shutdown
      void NewFrame();                                                        // ImGui_ImplVulkan_NewFrame
      void Record(VkCommandBuffer, ImDrawData*);                              // ImGui_ImplVulkan_RenderDrawData into whatever rendering is begun
      void RebuildFonts();                                                    // after Theme::AtlasDirty: ImGui_ImplVulkan_DestroyFontsTexture + create
      Result<ImTextureID> Bind(VkImageView, VkImageLayout, VkSampler) override;  // ImGui_ImplVulkan_AddTexture
      void Unbind(ImTextureID) override;                                      // ImGui_ImplVulkan_RemoveTexture, deferred to the retire queue at LastSubmitted
      VkSampler DefaultSampler() const;
  };
  ```
  The backend's Vulkan function loading goes through `ImGui_ImplVulkan_LoadFunctions` with a loader that resolves through the device's `InstanceFn`/`Fn()` tables (volk globals are not assumed to point at this device's instance).
- Produces (`Tests/Ui/Gpu/Support/UiGpuFixture.h`): `struct UiGpuFixture { std::unique_ptr<Gpu::Device> device; std::unique_ptr<Gpu::UploadContext> upload; std::unique_ptr<Gpu::FrameRing> frames; std::unique_ptr<ImGuiVulkanBackend> backend; HeadlessImGui imgui; std::unique_ptr<TexturePool> textures; }` with `MakeUiGpuFixture()` requiring the validation layer unless `ONYX_NO_VALIDATION=1`, and `Result<TestKit::Image8> RenderImGuiFrame(UiGpuFixture&, Gpu::RenderTarget&, const std::function<void()>& draw)` (NewFrame → `draw()` → Render → `frames->Begin` → `target.Begin(cmd, clear)` → `backend->Record` → `target.End` → `End` → `ReadbackColor`).

- [ ] **Step 1: Write the failing tests** (`Ui.Gpu`): `TexturePool::Create` of a 4×4 checker returns an id with `Size == {4,4}`; `Update` with a different size fails `ui.texture.size`; `Remove` then `Retire().Pending() > 0`; `RenderImGuiFrame` drawing `ImGui::Image(id, {64,64})` yields a non-uniform readback whose top-left 64×64 block contains both checker colours; drawing nothing yields the clear colour everywhere; `RebuildFonts` after `BuildFonts` leaves zero validation messages; backend `Create` on a target format of `R8G8B8A8_UNORM` (the test target) works.
- [ ] **Step 2: Run** → FAIL. **Implement** (`TexturePool` ported from `legacy/Source/App/TexturePool.cpp` onto `Gpu`; backend wrapping `imgui_impl_vulkan` with `ImGui_ImplVulkan_InitInfo::UseDynamicRendering = true` and `PipelineRenderingCreateInfo` from `BackendOptions`).
- [ ] **Step 3: Run** → PASS, zero validation messages. **Verify** `check.py --jobs 4 --ctest-regex "Ui"`.
- [ ] **Step 4: Commit** `feat(ui): texture pool, texture binder and the ImGui Vulkan backend`

## Task 5: Widgets

**Files:**
- Create: `Source/Ui/include/Onyx/Ui/Widgets.h`, `Source/Ui/Widgets.cpp`
- Test: `Tests/Ui/Widgets.cpp`

**Interfaces:**
- Consumes: Task 3 `Theme` (`Applied().style`, `TextForSurface`), Task 1 `Icon(IconId)`.
- Produces (`Widgets.h`, namespace `Onyx::Ui::Widgets`): the set the stock panels need twice (rule §8.5): `struct IconButtonOptions { ImVec2 size{}; const char* tooltip = nullptr; bool selected = false; bool disabled = false; };` `bool IconButton(const char* id, const char* glyph, const IconButtonOptions& = {});` (own frame, ink-centred, from v1) `bool TreeRow(const char* id, const char* label, const char* glyph, const glm::vec4& color, ImGuiTreeNodeFlags flags, bool selected);` (v1 `ColoredTreeNode`) `bool ToolbarButton(const char* label, bool selected = false);` `void Hint(const char* text);` (dim text) `void Spinner(const char* id, float radius, float thickness);` `struct ClippedList { ImGuiListClipper clipper; }` helpers: `template <class F> void ForEachVisible(size_t count, float rowHeight, F&& draw)` (wraps `ImGuiListClipper` so every clipped panel shares one usage) `bool SplitterH(const char* id, float* left, float* right, float minLeft, float minRight);` `void StatusText(Services::Severity, const char* text);` `struct Metrics { float smallButtonInset, iconButtonSize, rowHeight; } Metrics(const Theme&);`.

- [ ] **Step 1: Write the failing tests** (headless): each widget draws without ImGui asserts inside a `HeadlessImGui` frame and emits draw commands; `IconButton` returns true when the item is clicked (simulate with `io.AddMousePosEvent`/`AddMouseButtonEvent` across two frames); `ForEachVisible(100000, 20, f)` calls `f` for at most the visible rows (fewer than 100); `SplitterH` respects the minimums.
- [ ] **Step 2: Run** → FAIL. **Implement** (port from `legacy/Source/App/Widgets.cpp` what the list names; drop the hover-prediction contrast wrappers).
- [ ] **Step 3: Run** → PASS. **Verify** `check.py --jobs 4 --ctest-regex "Ui.Unit"`.
- [ ] **Step 4: Commit** `feat(ui): widgets the stock panels share`

## Task 6: Workspace panels — DocumentBrowser, Inspector, Problems, Log, Settings

**Files:**
- Create: `Source/Ui/include/Onyx/Ui/Panels/{DocumentBrowser,Inspector,Problems,LogPanel,SettingsPanel}.h`, `Source/Ui/Panels/{DocumentBrowser,Inspector,Problems,LogPanel,SettingsPanel}.cpp`, `Source/Ui/include/Onyx/Ui/Selection.h`, `Source/Ui/Selection.cpp`
- Test: `Tests/Ui/Panels/{DocumentBrowser,Inspector,Problems,LogPanel,SettingsPanel}.cpp`, `Tests/Ui/Support/ObxWorkspace.h`

**Interfaces:**
- Consumes: Tasks 2, 3, 5; `Modules::Workspace` (`Documents()`, `Get`, `Events()`, `Catalog()`, `Decoders()`, `ModuleSettings`, `Modules()`), `Document{roots, diags, ready}`, `Domain::AssetEntry`, `Types::TypeSpec`, `Capabilities`, `Schema` (`DecoderRegistry::Schema(type)` → `std::shared_ptr<const Schema::Schema>`), `Services::Log` sinks and `LogEntry`, `SettingsSchema::Entries()`, `Selection` events; `Examples/OnyxBox` `Writer` to build fixtures.
- Produces (`Selection.h`): `struct Selection { Modules::DocumentId doc = 0; Modules::NodePath node; bool Empty() const; };` `class SelectionModel { void Set(Selection, Services::EventBus&); /* posts Modules::SelectionChanged */ const Selection& Current() const; }` owned by the host and shared through `UiContext`?—no: `SelectionModel` is owned by `DocumentBrowser` and read through `DocumentBrowser::Selection()`; other panels find the browser through `UiContext::panels.Find("onyx.browser")`.
- Produces panels (all `IPanel`, ids `onyx.browser`, `onyx.inspector`, `onyx.problems`, `onyx.log`, `onyx.settings`; titles `Documents`, `Inspector`, `Problems`, `Log`, `Settings`):
  - `DocumentBrowser`: one tree per document; roots and children drawn through `Widgets::ForEachVisible` over a flattened visible-row list rebuilt only when a document's `ready` flips or a node expands/collapses (no per-frame strings: labels are `AssetEntry::name` c-strings, counts cached in the row); single click selects (posts `SelectionChanged`), double-click or Enter calls `std::function<void(UiContext&, const Selection&)> onOpen` (set by the viewer host, Task 7); context menu with `Close document`; `Failed` nodes drawn with the error colour; `Problems` count badge per document.
  - `Inspector`: shows the selected entry's `TypeSpec` (key, label, media, icon, colour), `ByteRange`, `Capabilities` of the type (from `Decoders().Of`), the `Schema` tree for the type when one is registered (field name, type, offset, size), and the module's `ModuleInfo`.
  - `Problems`: drains each document's `DiagSink::Snapshot()` into a clipped list with severity icons; a filter row (Info/Warning/Error toggles); click navigates the browser selection when the diag carries `ByteRef` matching an entry source.
  - `LogPanel`: installs a `Log::Default()` sink at construction (removed in the destructor) into its own ring of `kCapacity = 4096` entries (mutex, no copy per frame), clipped, level filter, `Clear`, auto-scroll.
  - `SettingsPanel`: a tab per schema: `Appearance` (`Theme::DescribeSettings` rendered through `Theme::Mutate`), one tab per module (`workspace.ModuleSettings(id)` entries rendered by `Kind` with their bounds, writes through `workspace.Settings()`), `Apply`/`Reset to defaults`.
- Produces (`Tests/Ui/Support/ObxWorkspace.h`): `struct ObxWorkspace { Modules::Workspace workspace; Modules::DocumentId doc; std::filesystem::path file; }` `ObxWorkspace MakeObxWorkspace(const std::filesystem::path& dir, size_t flatChildren = 8)` writing an `.obx` with the OnyxBox `Writer` (image, text, mesh, table, blob entries and `flatChildren` blobs) and opening it synchronously.

- [ ] **Step 1: Write the failing tests** (headless, with `ObxWorkspace`): the browser draws one row per visible node and selecting posts `SelectionChanged` with the right `NodePath`; with `flatChildren = 20000` one frame draws fewer than 200 rows and takes under 20 ms (measure with `std::chrono`, assert loosely `< 100 ms`); double-click calls `onOpen` with the selection; `Inspector` shows the type key and capabilities text of the selected image entry (assert on `ImGui` draw-list text through a helper that collects `ImDrawList` text? — simpler: expose `Inspector::Describe(const Selection&, UiContext&) -> InspectorModel` as a pure view-model and test that); `Problems` lists a diag reported into the document's sink; `LogPanel` receives a `Log::Default().Write` entry and drops the oldest past capacity; `SettingsPanel::Model(UiContext&)` lists the Appearance schema plus the `obx` module schema with its `obx.strictToc` entry.
- [ ] **Step 2: Run** → FAIL. **Implement**; each panel exposes a pure `Model`/`Describe` function the test can assert on and a thin `Draw` over it.
- [ ] **Step 3: Run** → PASS. **Verify** `check.py --jobs 4 --ctest-regex "Ui.Unit"`.
- [ ] **Step 4: Commit** `feat(ui): document browser, inspector, problems, log and settings panels`

## Task 7: Viewers — Image, Text, Hex, Table, Audio — and the opening pipeline

**Files:**
- Create: `Source/Ui/include/Onyx/Ui/Viewers/{ImageView,TextView,HexView,TableView,AudioView,DocumentTabs,Opening}.h`, `Source/Ui/Viewers/{ImageView,TextView,HexView,TableView,AudioView,DocumentTabs,Opening,StockViewers}.cpp`, `Source/Ui/include/Onyx/Ui/AudioOutput.h`
- Test: `Tests/Ui/Viewers/{Opening,DocumentTabs,TextView,HexView,TableView}.cpp`, `Tests/Ui/Gpu/ImageView.cpp`

**Interfaces:**
- Consumes: Tasks 2–6; `DecoderRegistry` decode functions (`DecodeImage/Text/Scene/Audio/Table`, `DecodeContext`), `Workspace::Jobs()` (`Submit(lane, work, done)`), `Vfs::Reader` over the entry's `ByteRange`, `Domain::{Texture,Text,Table,Audio}`.
- Produces (`Opening.h`): `inline constexpr uint64_t kDecodeLane = 0;` `Result<void> OpenEntry(UiContext&, const Selection&, std::function<void(std::unique_ptr<IDocumentView>)> place)` — routes through `ViewerRegistry::Route`; when the route needs a decoded payload, submits the decode on `kDecodeLane` (`DecodeContext` built from the document and a per-open `DiagSink` drained into the document's sink), then on `Done` (UI thread, via `Jobs().Pump()`) builds the `OpenRequest` and calls `spec.create`; `Raw` entries read bytes through the reader (capped at `kHexCap = 64 MiB`, the view reads lazily beyond that — keep a `Reader` instead); `ui.viewer.none` when nothing routes.
- Produces (`DocumentTabs.h`): `class DocumentTabs : public IPanel` (id `onyx.documents`, title `Viewer`): a tab bar of `IDocumentView`s; `void Add(std::unique_ptr<IDocumentView>)`, `void CloseActive()`, `void CloseAll()`, `void CloseForDocument(DocumentId)`, `IDocumentView* Active()`, `size_t Count() const`; subscribes to `DocumentClosed` to drop tabs; deferred deletion after the frame.
- Produces (`StockViewers.cpp`): `void RegisterStockViewers(ViewerRegistry&)` registering `onyx.image` (Image), `onyx.text` (Text), `onyx.hex` (Raw and Unknown, priority -10), `onyx.table` (Table), `onyx.audio` (Audio), and `onyx.scene` (Mesh, created by Task 8's factory — Task 7 registers a forward-declared `CreateSceneView` from `Viewers/SceneView.h` that Task 8 provides; until then a stub returning null is **not** acceptable: Task 7 registers only the five, Task 8 appends `onyx.scene`).
- Produces viewers (each `IDocumentView` with `Document()`/`Node()`): `ImageView` (zoom/pan/fit, alpha toggle, channel info, uses `TexturePool::Create`, `Remove` on destruction); `TextView` (read-only, clipped lines, line numbers, word wrap toggle, `Copy all`, search box; the text split into line offsets once); `HexView` (16 bytes per row, offset/hex/ASCII columns, clipped, goto offset, reads through `Vfs::Reader` on demand); `TableView` (ImGui table over `Domain::Table`: columns from the table's columns, sortable, clipped rows, cell copy); `AudioView` (waveform drawn from `Domain::Audio` samples (min/max per pixel column, cached per width), format info, play/pause/stop through `IAudioOutput` when the context provides one — `AudioOutput.h`: `class IAudioOutput { virtual Result<void> Play(const Domain::Audio&) = 0; virtual void Stop() = 0; virtual bool Playing() const = 0; virtual double Position() const = 0; };` the Shell/Media supply it in W4; `UiContext` gains `IAudioOutput* audio = nullptr`).

- [ ] **Step 1: Write the failing tests**: `OpenEntry` on the OnyxBox image entry decodes on the job lane and places an `ImageView` after `Pump()` (`Ui.Gpu`, since the view uploads); on the text entry places a `TextView` whose `Lines() == 3` for a three-line fixture (`Ui.Unit`); on a blob places a `HexView` whose `RowCount()` equals `ceil(size/16)` and `Row(1)` hex text equals the fixture's bytes; on the table entry places a `TableView` with the fixture's column names; a type with no viewer → `ui.viewer.none`; `DocumentTabs` adds, activates, closes active, closes all for a document on `DocumentClosed`.
- [ ] **Step 2: Run** → FAIL. **Implement** (image/text from `legacy/Source/Viewers/{ImageViewer,TextEditorViewer}.cpp` minus ColorTextEdit; hex and table new; audio new).
- [ ] **Step 3: Run** → PASS. **Verify** `check.py --jobs 4 --ctest-regex "Ui"`.
- [ ] **Step 4: Commit** `feat(ui): image, text, hex, table and audio viewers with the opening pipeline`

## Task 8: Viewport, SceneView, Transport, Timeline

**Files:**
- Create: `Source/Ui/include/Onyx/Ui/Viewers/SceneView.h`, `Source/Ui/include/Onyx/Ui/Panels/{Viewport,Transport,Timeline}.h`, `Source/Ui/include/Onyx/Ui/SceneSession.h`, `Source/Ui/Viewers/SceneView.cpp`, `Source/Ui/Panels/{Viewport,Transport,Timeline,AxisGizmo}.cpp`, `Source/Ui/SceneSession.cpp`
- Modify: `Source/Ui/Viewers/StockViewers.cpp` (append `onyx.scene`)
- Test: `Tests/Ui/Gpu/{Viewport,SceneView}.cpp`, `Tests/Ui/{Transport,Timeline,AxisGizmo}.cpp`

**Interfaces:**
- Consumes: `Render::SceneRenderer`, `GpuScene`, `View`, `Overlays`, `Camera`, `AnimationPlayer`, `Gpu::RenderTarget` (`Generation()`), `TexturePool::Register`, `UiContext::frame`.
- Produces (`SceneSession.h`): `class SceneSession { Result<void> Load(Render::SceneRenderer&, Gpu::UploadContext&, const Domain::Scene&, Services::DiagSink*); Render::GpuScene& Scene(); Render::Camera& Camera(); Render::AnimationPlayer& Player(); Render::Overlays& Overlays(); Render::ShadingMode& Mode(); bool HasAnimation() const; void Tick(float dt); /* advances the player, copies palette + worlds into the scene */ }` — the shared state of one open scene; owned by `SceneView`.
- Produces (`SceneView.h`): `class SceneView : public IDocumentView` (id `onyx.scene`, title = entry name): owns a `SceneSession`, a `Render::SceneRenderer` is **shared** per `Gpu::Device` through `UiContext::textures`? — no: the renderer is owned by `Viewport` (one per host); `SceneView` holds the session and draws its toolbar (shading mode, grid/skeleton toggles, focus) and an inspector list of parts (`DrawInfo`); `std::unique_ptr<IDocumentView> CreateSceneView(OpenRequest&)` registered as `onyx.scene` for `MediaKind::Mesh`.
- Produces (`Viewport.h`): `class Viewport : public IPanel` (id `onyx.viewport`, title `Viewport`): owns `SceneRenderer` (created lazily from `UiContext::gpu`), a `RenderTarget` sized to the panel, the registered `ImTextureID` (re-registered when `Generation()` changes); each `Draw`: finds the active `SceneView` through `panels.Find("onyx.documents")`, handles mouse orbit/pan/zoom and the `AxisGizmo` (ImGui-drawn tripod, click snaps `Camera::SnapToView`), records `SceneRenderer::Draw` into `UiContext::frame` (adds a wait on the session's `Ready()`), shows the resolve image; without `gpu`/`frame` draws a placeholder text; `RequestRedraw` only while animating or interacting.
- Produces (`Transport.h`, `Timeline.h`): `Transport` (id `onyx.transport`): play/pause/stop, clip combo, speed, loop mode, acting on the active session's `AnimationPlayer`; `Timeline` (id `onyx.timeline`): a scrubber over `Duration()` with frame ticks drawn on the draw list (no ImPlot), drag sets `SetTime`.
- Produces (`AxisGizmo.cpp`, header-private in `Panels`): `struct GizmoHit { std::optional<Render::CameraView> view; bool hovered; }` `GizmoHit DrawAxisGizmo(ImDrawList*, ImVec2 center, float radius, const glm::mat3& viewRotation)` ported from `legacy/Source/Rendering/AxisGizmo.cpp`.

- [ ] **Step 1: Write the failing tests**: `Ui.Gpu`: `Viewport` with the OnyxBox mesh document opened through `OpenEntry` renders (through `RenderImGuiFrame` with the host frame passed in `UiContext`) a non-uniform image inside the panel's rect and zero validation messages; a panel resize between two frames re-registers the texture (`TexturePool::Count()` stays 1, `Generation()` grew); `Ui.Unit`: `Transport::Model` reflects `Playing()`/clip names of `BuildAnimatedChain`; `Timeline` drag maps pixels to time within 1e-3; `DrawAxisGizmo` returns the `+X` view when clicked on the X cap.
- [ ] **Step 2: Run** → FAIL. **Implement** (orbit/pan/zoom from v1 `Viewport3D.cpp:447-636` minus the god-object parts; toolbar from `:638-749`).
- [ ] **Step 3: Run** → PASS. **Verify** `check.py --jobs 4 --ctest-regex "Ui"`.
- [ ] **Step 4: Commit** `feat(ui): viewport, scene view, transport and timeline`

## Task 9: Shell window, present path, backend glue and frame loop

**Files:**
- Create: `Source/Shell/include/Onyx/Shell/{NativeWindow,Presenter,ShellFrame}.h`, `Source/Shell/{NativeWindow,NativeWindowWin32,NativeWindowLinux,Presenter,ShellFrame}.cpp`
- Test: `Tests/Shell/Window/{NativeWindow,Presenter}.cpp`, `Tests/Shell/Window/Support/WindowFixture.h`

**Interfaces:**
- Consumes: GLFW 3.4, `imgui::glfw`, `Gpu::Device` (`presentSupport`, `instanceExtensions` from `glfwGetRequiredInstanceExtensions`), `Gpu::Swapchain`, `FrameRing`, `Queue`, `Ui::ImGuiVulkanBackend`, `Ui::Theme`, `Ui::FrameClock`.
- Produces (`NativeWindow.h`): `struct WindowOptions { std::string title; glm::ivec2 size{1280, 800}; bool borderless = true; bool visible = true; };` `class NativeWindow { static Result<std::unique_ptr<NativeWindow>> Create(const WindowOptions&); GLFWwindow* Handle() const; glm::ivec2 FramebufferSize() const; float ContentScale() const; bool ShouldClose() const; void SetShouldClose(bool); void PollEvents(); void WaitEvents(double timeoutSeconds); void SetTitle(std::string_view); void SetDropHandler(std::function<void(std::vector<std::filesystem::path>)>); void SetResizeHandler(std::function<void(glm::ivec2)>); Result<VkSurfaceKHR> CreateSurface(VkInstance); std::vector<const char*> RequiredInstanceExtensions() const; }` — `glfwInit` reference-counted inside (`shell.window` on failure); Win32 borderless decoration ported from `legacy/Source/App/Platform/NativeWindow_windows.cpp` (hit-test by a `TitleBarRect` the frame sets each frame, never by ImGui window names; style bits set once); Linux: plain decorated or borderless GLFW window.
- Produces (`Presenter.h`): `class Presenter { static Result<std::unique_ptr<Presenter>> Create(Gpu::Device&, VkSurfaceKHR, glm::ivec2 size); Result<std::optional<Gpu::SwapImage>> Acquire(Gpu::Frame&); /* adds the acquire semaphore wait and the present signal to the frame */ void BeginRendering(VkCommandBuffer, const Gpu::SwapImage&, const glm::vec4& clear); void EndRendering(VkCommandBuffer, const Gpu::SwapImage&); Result<void> Present(Gpu::FenceValue, const Gpu::SwapImage&); Result<void> Resize(glm::ivec2); VkFormat Format() const; }` over `Gpu::Swapchain` with per-frame binary semaphores and the v1 present rules (recreate on OUT_OF_DATE/SUBOPTIMAL/resize, every `VkResult` handled).
- Produces (`ShellFrame.h`): `class ShellFrame { ShellFrame(NativeWindow&, Gpu::Device&, Presenter&, Gpu::FrameRing&, Ui::ImGuiVulkanBackend&, Ui::Theme&, Ui::FrameClock&); Result<bool> Step(const std::function<void(Gpu::Frame&)>& drawUi); /* one frame: pacing (wait events unless continuous), theme Update/Commit outside the frame, font rebuild when dirty, ImGui NewFrame (glfw + vulkan backends), drawUi, Render, acquire, record, present; returns false when the window should close; handles swapchain recreate; a re-entrancy guard */ }`.
- Produces (`Tests/Shell/Window/Support/WindowFixture.h`): `MakeWindowFixture()` creating a hidden window (`visible = false`), a device with `presentSupport` and the window's extensions, a presenter, frames, backend, theme over an `Settings::Empty()`, clock; `REQUIRE`s or the probe main's 77.

- [ ] **Step 1: Write the failing tests** (`Shell.Window`): `NativeWindow::Create` hidden → `FramebufferSize()` matches the options; `CreateSurface` ok; `Presenter::Create` ok, `Acquire` yields an image, `Present` ok or `NeedsRecreate`; `ShellFrame::Step` five times drawing a window returns true each time with zero validation messages; `Resize` to a new size then `Step` ok; `SetShouldClose(true)` → `Step` returns false; the drop handler receives paths when invoked through the GLFW callback directly (call the registered callback by hand through a test hook `NativeWindow::SimulateDrop`).
- [ ] **Step 2: Run** → FAIL. **Implement** (port from `legacy/Source/App/Window.cpp` 480-620, 999-1182 and the platform files; no `exit()`, every failure a `Result`).
- [ ] **Step 3: Run** → PASS. **Verify** `check.py --jobs 4 --ctest-regex "Shell"`.
- [ ] **Step 4: Commit** `feat(shell): native window, presenter and the frame loop`

## Task 10: Shell composition — Dock, MenuBar, Recents, Dialogs, Lifecycle, Run

**Files:**
- Create: `Source/Shell/include/Onyx/Shell/{Shell,Dock,MenuBar,Recents,Dialogs,Lifecycle,StockCommands}.h`, `Source/Shell/{Shell,Dock,MenuBar,Recents,Dialogs,DialogsWin32,DialogsLinux,Lifecycle,StockCommands,ShellCli}.cpp`
- Test: `Tests/Shell/{Dock,MenuBar,Recents,StockCommands,ShellCli}.cpp`, `Tests/Shell/Window/Shell.cpp`

**Interfaces:**
- Consumes: Tasks 2–9; `Core::Cli` (`VerbTable`, `Run`, `Cli::StockVerbs()`), `Services::Settings` (`Load`, `SaveIfDirty`), `Paths::UserConfig`, `Modules::Workspace`, `IGameModule::Info().openFilters`.
- Produces (`Shell.h`):
  ```cpp
  struct ShellOptions { std::string appName; std::filesystem::path configDir; glm::ivec2 windowSize{1280, 800}; bool borderless = true; bool hiddenWindow = false; unsigned workerThreads = 2; };
  class Shell {
  public:
      explicit Shell(ShellOptions);
      Modules::Workspace& Workspace(); Ui::CommandRegistry& Commands(); Ui::ViewerRegistry& Viewers(); Ui::PanelHost& Panels(); Ui::Theme& Theme(); Services::Settings& Settings(); Core::Cli::VerbTable& Verbs();
      void AddExtension(std::unique_ptr<IShellExtension>);     // struct IShellExtension { virtual void Install(Shell&) = 0; }  (a module's Ui: registers viewers/panels/commands)
      void SetLifecycle(std::unique_ptr<LifecycleDelegate>);
      int Run(int argc, char** argv);      // argc > 1 → CLI through Verbs() (shell.args on bad usage); else GUI
      Result<void> Open(const std::filesystem::path&);          // workspace OpenAsync + recents + title
      Result<bool> Step();                 // one GUI frame for tests; false when closing
      void RequestClose();
  };
  ```
  `Run` GUI path: `Settings::Load(configDir/settings.toml)`, `imgui.ini` at `configDir/imgui.ini`, creates window/device/presenter/frames/backend/theme/textures/clock, `RegisterStockViewers`, adds the stock panels (browser, documents, inspector, problems, log, settings (hidden), viewport, transport, timeline), `StockCommands`, applies the default `Dock` layout when no `imgui.ini` exists, then loops `Step()`; close: `LifecycleDelegate::WillClose()` veto, `Settings::SaveIfDirty`, waits the queue idle through the device destructor order, returns 0. Periodic `SaveIfDirty` every 5 s of dirtiness.
- Produces (`Dock.h`): `struct DockSlot { std::string panelId; ImGuiDir side; float ratio; };` `struct DockLayout { std::vector<DockSlot> slots; std::string centerPanelId; }` `void ApplyDockLayout(ImGuiID dockspace, const DockLayout&, Ui::PanelHost&)` (DockBuilder); `DockLayout DefaultLayout()` (browser left 0.22, inspector right 0.25, problems+log bottom 0.25 tabbed, documents centre, transport+timeline under the centre 0.15).
- Produces (`MenuBar.h`): `void DrawMenuBar(Ui::UiContext&)` rendering `CommandRegistry::All()` grouped by `menuPath` (`File/Open…` → menu `File`, item `Open…`; nested `/` makes submenus; order = registration order; a `View` menu generated from `PanelHost` toggles; shortcuts shown through `FormatShortcut`).
- Produces (`Recents.h`): `class Recents { explicit Recents(Services::Settings&); void Add(const std::filesystem::path&, std::string_view moduleId); std::span<const RecentEntry> Entries() const; void Clear(); }` with `struct RecentEntry { std::filesystem::path path; std::string moduleId; std::string displayName; }`, `kMaxRecents = 10`, stored as `shell.recents` (string list `path|moduleId;...`) — no file stats at load.
- Produces (`Dialogs.h`): `class IDialogs { virtual std::optional<std::filesystem::path> OpenFile(std::span<const Modules::OpenFilter>) = 0; virtual std::optional<std::filesystem::path> OpenFolder() = 0; virtual std::optional<std::filesystem::path> SaveFile(std::string_view defaultName) = 0; };` `std::unique_ptr<IDialogs> NativeDialogs()` (Win32 `IFileOpenDialog`; Linux `zenity` through `popen` when found, else `xdg-desktop-portal` is out of scope: return nullopt with a log line) — dialogs run **between** frames (the command sets a pending request the loop services after `Render`).
- Produces (`Lifecycle.h`): `class LifecycleDelegate { virtual bool WillClose(Shell&) { return true; } virtual void FilesDropped(Shell&, std::span<const std::filesystem::path>) {} virtual void DidStart(Shell&) {} };`
- Produces (`StockCommands.h`): `void AddStockCommands(Ui::CommandRegistry&, Shell&)`: `onyx.file.open` (`Ctrl+O`, `File/Open…`), `onyx.file.openRecent.<n>` rebuilt from `Recents`, `onyx.file.closeActive` (`Ctrl+W`), `onyx.file.closeAll`, `onyx.file.exit` (`Alt+F4`, `File/Exit`), `onyx.view.resetLayout` (`View/Reset layout`), `onyx.view.settings` (`Ctrl+,`, `View/Settings…`), `onyx.view.toggle.<panelId>` generated per panel.
- Produces (`ShellCli.cpp`): `int RunCli(Shell&, int argc, char** argv)` building the `VerbTable` from `Cli::StockVerbs()` plus whatever `Verbs()` holds, `Cli::Run(workspace, table, argc, argv, std::cout, std::cerr)`.

- [ ] **Step 1: Write the failing tests** (`Shell.Unit`): `DefaultLayout` names every stock panel id once; `MenuBar` model: `MenuModel(commands, panels)` groups `File/Open…` and `File/Exit` under `File` in order and lists `View` toggles; `Recents` adds, caps at 10, dedups, persists through `Settings` and reloads; `AddStockCommands` registers the ids above with the shortcuts; `RunCli` with `probe <obx>` returns `kOk` and prints the module id. (`Shell.Window`): `Shell` with `hiddenWindow = true`: `Open(obx)`, `Step()` ×10 → `Workspace().Documents().size() == 1`, the `onyx.documents` panel has one tab after `Commands().Run("onyx.file.open")`?—no dialog in tests: open the image entry through `Ui::OpenEntry` on the browser's selection set programmatically, `Step()` ×5 → `DocumentTabs::Count() == 1`; a `LifecycleDelegate` returning false from `WillClose` keeps `Step()` returning true after `RequestClose()`; zero validation messages.
- [ ] **Step 2: Run** → FAIL. **Implement**; `Shell.cpp` stays under ~300 lines by delegating.
- [ ] **Step 3: Run** → PASS. **Verify** `check.py --jobs 4 --ctest-regex "Shell"`.
- [ ] **Step 4: Commit** `feat(shell): shell composition with dock, menu bar, recents, dialogs`

## Task 11: Examples — MinimalViewer and Embedded

**Files:**
- Create: `Examples/MinimalViewer/{CMakeLists.txt,Main.cpp}`, `Examples/Embedded/{CMakeLists.txt,Main.cpp,HostWindow.h,HostWindow.cpp}`, `Tests/Examples/CMakeLists.txt`
- Modify: `Examples/CMakeLists.txt` (`add_subdirectory(MinimalViewer)`, `add_subdirectory(Embedded)` under `if(ONYX_WITH_SHELL)` / `if(ONYX_WITH_UI)`), `Tests/CMakeLists.txt` (`add_subdirectory(Examples)` when `ONYX_BUILD_EXAMPLES`)

**Interfaces:**
- Produces: `MinimalViewer` (≈40 lines, spec §9): `Shell shell({.appName = "MinimalViewer", .configDir = Paths::UserConfig("MinimalViewer")}); shell.Workspace().AddModule(std::make_unique<OnyxBox::Module>()); return shell.Run(argc, argv);` with `onyx_install_assets(MinimalViewer)`.
- Produces: `Embedded`: a host with its own GLFW window, its own `ImGuiContext`, its own Vulkan instance/device/queue created by hand through volk (a second queue of the graphics family handed to Onyx), `Gpu::Device::Adopt`, its own `ImGuiVulkanBackend` instance acting as the `ITextureBinder` (proving the binder seam), a `Ui::PanelHost` with `DocumentBrowser` + `Viewport` + `DocumentTabs`, a `FrameRing` it owns; it draws its own ImGui window ("Host") beside Onyx's panels and presents through its own swapchain code (a minimal copy, not `Shell::Presenter`); `--frames N` exits after N frames (CI build-only runs `--frames 3` only when a device exists, else exit 77).
- Produces: `Tests/Examples/CMakeLists.txt`: `Examples.Build` = running `MinimalViewer --help` (exit 0, prints the verb usage from `Cli::Run`) and `Embedded --frames 3` with `SKIP_RETURN_CODE 77`.

- [ ] **Step 1: Write the two examples and the ctest entries.**
- [ ] **Step 2: Verify** `check.py --jobs 4 --ctest-regex "Examples|Shell"` green; run `MinimalViewer` by hand once on this machine, open `Tests/Golden/obx/sample.obx` (or a file written by the test fixture) through `File/Open…`, double-click the image and the mesh entries, confirm the Image viewer and the Viewport show them, close through the window button; record the manual check in the report (the Embedded example's draw is checked the same way, by eye, on Windows).
- [ ] **Step 3: Commit** `feat(examples): MinimalViewer and the Embedded host`

## Task 12: CI and docs for W3

**Files:**
- Modify: `.github/workflows/ci.yml` (Linux: `libglfw3-dev` is not needed (GLFW builds from source) but `libxkbcommon-dev libwayland-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl-dev` are; `Shell.Window`/`Examples.Build` SKIP without a display), `README.md` (Status W0–W3; "Running the shell" and "Drawing Onyx inside your own frame" sections), `CHANGELOG.md` (`### W3`), `CLAUDE.md` (Tools: Ui/Shell suites, `ONYX_NO_VALIDATION`), `tools/README.md`, `docs/superpowers/specs/2026-10-30-onyx-v2-design.md` (§8.1 `UiContext` gained `frame`, `viewers`, `panels`, `clock`, `audio`; §9 `Shell::Step`; §16 W3 row location notes).

- [ ] **Step 1: Write the edits**; validate the YAML.
- [ ] **Step 2: Verify** `py -3 tools/check.py --preset windows-debug --jobs 4` fully green; on WSL `python3 tools/check.py --preset linux-debug --jobs 4` with `ONYX_NO_VALIDATION=1` (Shell.Window and Examples.Build skip without a display; Ui.Unit/Ui.Gpu run on lavapipe).
- [ ] **Step 3: Commit** `docs: describe the Ui and Shell layers and the W3 status`

---

## Self-review notes

- Spec coverage: §8.1 contracts (T2, `UiContext` extended: `frame`, `viewers`, `panels`, `clock`, `audio` — rulings), §8.2 registry and routing (T2, T7), §8.3 stock panels (T6, T7, T8; Gallery not registered by default → not built in W3, deferred to W4 with Prism's UI as the second consumer), §8.4 commands and shortcuts (T2, T10), §8.5 theme single writer, widgets, icons, textures, ImGui ownership, FrameClock (T3, T5, T1, T4, T2), §9 Shell composition, dialogs, lifecycle, CLI-or-GUI `Run` (T9, T10), §14 MinimalViewer and Embedded (T11), §16 gate (T10 window test opens OnyxBox; theme and settings tests in T3/T6; Embedded builds, draws by eye on Windows, build-only on CI via `--frames 3`/77).
- Rulings embedded: ImPlot and ImGuiColorTextEdit dropped (not in the spec's dependency list; Timeline draws on the draw list, TextView is a clipped read-only viewer); the Audio viewer plays through an `IAudioOutput` the Media layer implements in W4; `SceneRenderer` is owned by the `Viewport`, scene state by `SceneSession` inside `SceneView`; Win32 custom title bar hit-testing uses a rect the frame publishes, never ImGui window names; dialogs run between frames; the Shell exposes `Step()` for tests and `hiddenWindow` for CI; the Embedded host creates its own device through volk and adopts it, proving §6; `UiContext::frame` is the host's current `Gpu::Frame` so the Viewport records into the host's command buffer (one submit per host frame).
- Type consistency: `UiContext` fields as in T2 are what T6–T10 consume; `OpenRequest` (T2) feeds the T7 viewers and T8's `CreateSceneView`; `ITextureBinder` (T4) is implemented by `ImGuiVulkanBackend` (T4) and by the Embedded host (T11); `Shell::Step` (T10) is what T11's `--frames` and the window tests use.
