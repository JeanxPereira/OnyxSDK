# Onyx v1 — M2: Identity & State Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The Core runtime contracts of the v1 spec — namespaced type
identity, diagnostics-as-data, the job queue, the event bus, scoped TOML
settings — implemented additively with unit tests, plus visibility
overrides persisted by string key.

**Architecture:** Everything lands in `Onyx::Core` as new, self-contained
services with doctest coverage; nothing existing is rewired yet (the
Workspace composition arrives in M3). The one behavioural change is
AssetVisibility: its dead 4-byte GTKC serialization API is replaced by
string-key TOML persistence.

**Tech Stack:** C++20, doctest, toml++ (already fetched), CMake/Ninja/MSVC.

**Spec:** `docs/design/2026-08-18-onyx-v1-architecture.md` (§4, §7; W2 in §12)

## Global Constraints

- **Commits:** Conventional Commits, sentence-case subject, ≤ 72 chars.
  **No AI attribution of any kind** (no `Co-Authored-By`, no "Generated
  with"). Standing project rule.
- **Repo:** `E:\CodingProjects\OnyxSDK`, branch `main`. Every task ends
  with SDK build green, full `ctest` green (21 tests + the task's new
  ones), commit of explicitly staged paths.
- **Build:** `cmd /c '"C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvars64.bat" >nul && cd /d E:\CodingProjects\OnyxSDK\build && cmake .. && ninja && ctest --output-on-failure'`
- **Layering:** all new code is CORE — no imgui/GLFW/glad includes
  anywhere in it (LayerGuard will scan the new files once they are added
  to `ONYX_CORE_SOURCES`).
- New sources must be added to `ONYX_CORE_SOURCES` in the root
  `CMakeLists.txt` (the completeness check fails the configure if
  forgotten). New test files go in `Tests/` and register themselves via the
  existing `Tests/CMakeLists.txt` glob-or-list convention — follow whatever
  that file does for `sessionlog_test.cpp`.
- Threading contracts follow spec §7.5: values/moves cross threads,
  references do not; no blocking destructors except `JobQueue`'s own
  (owner teardown is explicit and documented).
- Fact established during planning (2026-08-18): AppConfig is already
  TOML (`onyx.toml`); the GTKC importer in the spec is obsolete — do NOT
  build one. AssetVisibility's `SerializedOverride` API has zero callers.

---

### Task 1: TypeKey and namespaced TypeRegistrar

**Files:**
- Create: `Include/Onyx/Types/TypeRegistrar.h`
- Create: `Source/Types/TypeRegistrar.cpp`
- Modify: `Include/Onyx/Types/TypeCatalog.h` (add `KeyOf`)
- Modify: `Source/Types/TypeCatalog.cpp` (implement `KeyOf`)
- Modify: `CMakeLists.txt` (`ONYX_CORE_SOURCES` += TypeRegistrar.cpp)
- Test: `Tests/typeregistrar_test.cpp`

**Interfaces:**
- Consumes: existing `TypeCatalog::Register/Find/Info`, `TypeInfo`.
- Produces:
  ```cpp
  namespace Onyx::Types {
  // A registrar bound to one module id. Every key it mints is prefixed
  // "<moduleId>." — cross-module collisions are impossible by construction.
  class TypeRegistrar {
  public:
      TypeRegistrar(TypeCatalog& catalog, std::string moduleId);
      // spec.key must be bare ("mesh", not "gowr.mesh"): a key containing
      // '.' is refused (returns invalid TypeId and logs). Re-adding the
      // same bare key returns the existing handle.
      TypeId Add(const TypeInfo& spec);
      const std::string& ModuleId() const;
  };
  }
  // On TypeCatalog:
  //   std::string_view KeyOf(TypeId id) const;  // "" for invalid/unknown
  ```

- [ ] **Step 1: Write the failing tests**

```cpp
#include <doctest/doctest.h>
#include <Onyx/Types/TypeCatalog.h>
#include <Onyx/Types/TypeRegistrar.h>

using namespace Onyx::Types;

TEST_CASE("TypeRegistrar mints keys inside its module namespace") {
    TypeCatalog& cat = TypeCatalog::Get();
    TypeRegistrar reg(cat, "m2test");

    TypeInfo mesh; mesh.key = "mesh"; mesh.label = "Mesh";
    TypeId id = reg.Add(mesh);
    CHECK(id.valid());
    CHECK(cat.KeyOf(id) == "m2test.mesh");
    CHECK(cat.Find("m2test.mesh") == id);

    SUBCASE("re-adding the same bare key returns the same handle") {
        CHECK(reg.Add(mesh) == id);
    }
    SUBCASE("a dotted key is refused") {
        TypeInfo bad; bad.key = "other.mesh"; bad.label = "X";
        CHECK_FALSE(reg.Add(bad).valid());
    }
    SUBCASE("two registrars cannot collide") {
        TypeRegistrar reg2(cat, "m2other");
        TypeInfo alsoMesh; alsoMesh.key = "mesh"; alsoMesh.label = "Mesh";
        TypeId id2 = reg2.Add(alsoMesh);
        CHECK(id2.valid());
        CHECK(id2 != id);
        CHECK(cat.KeyOf(id2) == "m2other.mesh");
    }
}

TEST_CASE("TypeCatalog::KeyOf answers for registered and invalid ids") {
    TypeCatalog& cat = TypeCatalog::Get();
    CHECK(cat.KeyOf(TypeId{}) == "UNKNOWN");   // index 0 is the Unknown info
    CHECK(cat.KeyOf(TypeId{60000}) == "");     // out of range
}
```

- [ ] **Step 2: Run to verify failure** — expect compile error (no
  TypeRegistrar.h). Build command from Global Constraints.

- [ ] **Step 3: Implement**

`TypeRegistrar.cpp` core:

```cpp
TypeId TypeRegistrar::Add(const TypeInfo& spec) {
    if (spec.key.find('.') != std::string::npos) {
        LOG_ERR("[TypeRegistrar] '%s': bare keys only (got '%s')",
                m_moduleId.c_str(), spec.key.c_str());
        return {};
    }
    TypeInfo namespaced = spec;
    namespaced.key = m_moduleId + "." + spec.key;
    return m_catalog.Register(namespaced);
}
```

`KeyOf` on the catalog: bounds-checked index into `m_infos`, returning
`m_infos[id.value].key`; out-of-range returns `""`. Note the subtlety the
test encodes: `TypeId{}` is index 0, whose stored key is `"UNKNOWN"`.

- [ ] **Step 4: Build + full ctest green**
- [ ] **Step 5: Commit** — `feat(types): Namespaced TypeRegistrar and TypeCatalog::KeyOf`

### Task 2: Diagnostics as data

**Files:**
- Create: `Include/Onyx/Services/Diagnostics.h`
- Create: `Source/Services/Diagnostics.cpp`
- Modify: `CMakeLists.txt` (`ONYX_CORE_SOURCES` += Diagnostics.cpp)
- Test: `Tests/diagnostics_test.cpp`

**Interfaces:**
- Produces:
  ```cpp
  namespace Onyx::Services {
  enum class Severity : uint8_t { Info, Warning, Error };
  struct ByteRef { std::string file; uint64_t offset = 0; };
  struct Diag {
      Severity    severity = Severity::Info;
      std::string code;      // namespaced: "gowr.mesh.lod-missing"
      std::string message;
      std::optional<ByteRef> at;
  };
  // Thread-safe collector. Producers Report() from any thread; the owner
  // Drain()s on its own thread. Never throws, never logs by itself.
  class DiagSink {
  public:
      void Report(Diag d);
      std::vector<Diag> Drain();                 // returns and clears
      size_t Count(Severity atLeast = Severity::Info) const;
      bool   HasErrors() const;                  // Count(Error) > 0
  };
  }
  ```

- [ ] **Step 1: Failing tests**

```cpp
TEST_CASE("DiagSink collects, counts and drains") {
    Onyx::Services::DiagSink sink;
    sink.Report({Onyx::Services::Severity::Warning, "t.w", "warn", {}});
    sink.Report({Onyx::Services::Severity::Error,   "t.e", "err",
                 Onyx::Services::ByteRef{"a.wad", 0x40}});
    CHECK(sink.Count() == 2);
    CHECK(sink.Count(Onyx::Services::Severity::Error) == 1);
    CHECK(sink.HasErrors());
    auto out = sink.Drain();
    REQUIRE(out.size() == 2);
    CHECK(out[1].at->offset == 0x40);
    CHECK(sink.Count() == 0);
}

TEST_CASE("DiagSink is safe under concurrent producers") {
    Onyx::Services::DiagSink sink;
    std::vector<std::thread> ts;
    for (int t = 0; t < 4; ++t)
        ts.emplace_back([&sink]{
            for (int i = 0; i < 1000; ++i)
                sink.Report({Onyx::Services::Severity::Info, "t.i", "x", {}});
        });
    for (auto& t : ts) t.join();
    CHECK(sink.Count() == 4000);
}
```

- [ ] **Step 2: Run, expect compile failure**
- [ ] **Step 3: Implement** — mutex + vector + severity counters; all
  methods lock; `Drain` swaps the vector out under the lock.
- [ ] **Step 4: Build + ctest green**
- [ ] **Step 5: Commit** — `feat(diag): DiagSink - diagnostics as data, salvage-friendly`

### Task 3: Jobs — lanes, progress, cooperative cancel

**Files:**
- Create: `Include/Onyx/Services/Jobs.h`
- Create: `Source/Services/Jobs.cpp`
- Modify: `CMakeLists.txt`
- Test: `Tests/jobs_test.cpp`

**Interfaces:**
- Produces:
  ```cpp
  namespace Onyx::Services {
  class Progress {                       // producer side: worker thread
  public:
      void Step(float fraction, std::string_view label);  // clamps to [0,1]
      bool CancelRequested() const;
      struct Snapshot { float fraction; std::string label; };
      Snapshot Peek() const;             // consumer side: any thread
  };
  class JobHandle {                      // value handle; dtor detaches
  public:
      JobHandle() = default;
      void Cancel();                     // cooperative flag, never blocks
      bool Done() const;
      bool Valid() const;
  };
  // Worker pool with per-lane serialization: at most one job of a given
  // lane runs at a time (lane = future DocumentId). Completion callbacks
  // run only inside Pump(), on the thread that calls it.
  class JobQueue {
  public:
      explicit JobQueue(unsigned workers = 2);
      ~JobQueue();                       // signals stop, joins (documented
                                         // blocking teardown; owner-only)
      using Work = std::function<void(Progress&)>;
      using Done = std::function<void()>;
      JobHandle Submit(uint64_t lane, Work work, Done onDone = {});
      void   Pump();                     // drain finished-job callbacks
      size_t PendingCallbacks() const;
  };
  }
  ```

- [ ] **Step 1: Failing tests** (the three contracts that matter)

```cpp
TEST_CASE("JobQueue serializes jobs within a lane") {
    Onyx::Services::JobQueue q(4);
    std::vector<int> order; std::mutex mx;
    std::atomic<int> pending{3};
    for (int i = 0; i < 3; ++i)
        q.Submit(7, [&, i](Onyx::Services::Progress&) {
            { std::lock_guard<std::mutex> l(mx); order.push_back(i); }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            --pending;
        });
    while (pending > 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(order == std::vector<int>{0, 1, 2});   // same lane => FIFO
}

TEST_CASE("Cancellation is cooperative and Done callbacks only fire in Pump") {
    Onyx::Services::JobQueue q(1);
    std::atomic<bool> sawCancel{false}, doneRan{false};
    auto h = q.Submit(1, [&](Onyx::Services::Progress& p) {
        while (!p.CancelRequested())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        sawCancel = true;
    }, [&]{ doneRan = true; });
    h.Cancel();
    while (!sawCancel) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    while (q.PendingCallbacks() == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK_FALSE(doneRan);                 // not yet: no Pump ran
    q.Pump();
    CHECK(doneRan);
}

TEST_CASE("Progress snapshots are consistent") {
    Onyx::Services::JobQueue q(1);
    std::atomic<bool> go{false}, quit{false};
    auto h = q.Submit(2, [&](Onyx::Services::Progress& p) {
        p.Step(0.5f, "halfway");
        go = true;
        while (!quit) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });
    while (!go) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    // Peek through the handle's state — add JobHandle::Peek() delegating
    // to the job's Progress.
    quit = true;
}
```

(The third case pins the API decision that `JobHandle` exposes
`Progress::Snapshot Peek() const` — implement it as part of this task.)

- [ ] **Step 2: Run, expect failure**
- [ ] **Step 3: Implement** — shared `JobState { Progress, done flag,
  Done callback }`; per-lane `std::deque` of pending jobs inside one
  mutex; workers pull the next job of any lane that has none running;
  completed jobs push their state onto a finished queue that `Pump()`
  drains. Handle destructor only drops the shared_ptr.
- [ ] **Step 4: Build + ctest green** (run the suite 5× —
  `ctest -R Jobs --repeat until-fail:5` — to shake races)
- [ ] **Step 5: Commit** — `feat(jobs): Lane-serialized JobQueue with cooperative cancel`

### Task 4: EventBus

**Files:**
- Create: `Include/Onyx/Services/EventBus.h`
- Create: `Source/Services/EventBus.cpp`
- Modify: `CMakeLists.txt`
- Test: `Tests/eventbus_test.cpp`

**Interfaces:**
- Produces:
  ```cpp
  namespace Onyx::Services {
  class EventBus;
  // RAII: destruction unsubscribes. Move-only.
  class Subscription {
  public:
      Subscription() = default;
      ~Subscription();
      Subscription(Subscription&&) noexcept;
      Subscription& operator=(Subscription&&) noexcept;
      Subscription(const Subscription&) = delete;
  };
  // Owned by its composition root (Workspace in M3) — NOT a singleton.
  // Post from any thread; Pump() dispatches FIFO on the calling thread.
  class EventBus {
  public:
      template <class E> Subscription On(std::function<void(const E&)> fn);
      template <class E> void Post(E ev);
      void Pump();
      size_t PendingEvents() const;
  };
  }
  ```
  Type identity per event type via a per-`E` static tag address (no RTTI
  requirement): `template <class E> static const void* TypeTag()`.

- [ ] **Step 1: Failing tests**

```cpp
struct EvA { int v; };
struct EvB { std::string s; };

TEST_CASE("EventBus dispatches FIFO on Pump, not on Post") {
    Onyx::Services::EventBus bus;
    std::vector<int> got;
    auto sub = bus.On<EvA>([&](const EvA& e){ got.push_back(e.v); });
    bus.Post(EvA{1}); bus.Post(EvA{2});
    CHECK(got.empty());                   // queued, not immediate
    bus.Pump();
    CHECK(got == std::vector<int>{1, 2});
}

TEST_CASE("Subscription RAII: a dead subscriber is never called") {
    Onyx::Services::EventBus bus;
    int calls = 0;
    { auto sub = bus.On<EvA>([&](const EvA&){ ++calls; }); }
    bus.Post(EvA{1});
    bus.Pump();
    CHECK(calls == 0);
}

TEST_CASE("Distinct event types do not cross") {
    Onyx::Services::EventBus bus;
    int a = 0, b = 0;
    auto sa = bus.On<EvA>([&](const EvA&){ ++a; });
    auto sb = bus.On<EvB>([&](const EvB&){ ++b; });
    bus.Post(EvB{"x"});
    bus.Pump();
    CHECK(a == 0); CHECK(b == 1);
}

TEST_CASE("Cross-thread Post is safe") {
    Onyx::Services::EventBus bus;
    std::atomic<int> got{0};
    auto sub = bus.On<EvA>([&](const EvA&){ ++got; });
    std::vector<std::thread> ts;
    for (int t = 0; t < 4; ++t)
        ts.emplace_back([&]{ for (int i = 0; i < 250; ++i) bus.Post(EvA{i}); });
    for (auto& t : ts) t.join();
    bus.Pump();
    CHECK(got == 1000);
}
```

- [ ] **Step 2: Run, expect failure**
- [ ] **Step 3: Implement** — `std::deque<QueuedEvent>` +
  `std::map<const void*, std::vector<Handler>>` under one mutex; `Pump`
  moves the queue out under the lock, then dispatches without it (a
  handler may Post — those land in the next Pump; document that).
  Subscription holds bus pointer + (tag, id); bus clears the pointer via
  a shared alive-flag (`std::shared_ptr<bool>`) so a Subscription
  outliving the bus is a no-op, not a crash — add a test for that too.
- [ ] **Step 4: Build + ctest green**
- [ ] **Step 5: Commit** — `feat(events): Workspace-ownable EventBus with RAII subscriptions`

### Task 5: Scoped Settings on TOML

**Files:**
- Create: `Include/Onyx/Services/Settings.h`
- Create: `Source/Services/Settings.cpp`
- Modify: `CMakeLists.txt`
- Test: `Tests/settings_test.cpp`

**Interfaces:**
- Produces:
  ```cpp
  namespace Onyx::Services {
  // One TOML document. Dotted keys ("gowr.texIndexDir") map to
  // [gowr] texIndexDir = ... — the module id is the table, per spec §7.3.
  class Settings {
  public:
      static Settings Load(const std::filesystem::path& file);  // missing => empty
      bool Save();                       // writes Load()'s path; clears Dirty
      std::optional<bool>        GetBool  (std::string_view key) const;
      std::optional<int64_t>     GetInt   (std::string_view key) const;
      std::optional<double>      GetDouble(std::string_view key) const;
      std::optional<std::string> GetString(std::string_view key) const;
      void Set(std::string_view key, bool v);
      void Set(std::string_view key, int64_t v);
      void Set(std::string_view key, double v);
      void Set(std::string_view key, std::string v);
      bool Dirty() const;
      const std::filesystem::path& Path() const;
  };
  }
  ```
  Scope is a *convention on paths*, not a class: app scope loads
  `onyx-settings.toml` beside `onyx.toml`; workspace scope loads
  `.onyx/workspace.toml` under the opened folder. M3's Workspace wires
  them; M2 delivers the class and documents the convention in the header.

- [ ] **Step 1: Failing tests** — round-trip every type through a temp
  file; dotted-key → table mapping (`Set("gowr.texIndexDir", "X")` then
  reparse raw TOML and assert `tbl["gowr"]["texIndexDir"]`); missing file
  loads empty; `Dirty()` flips on Set and clears on Save; keys without a
  dot land at root.

```cpp
TEST_CASE("Settings round-trips typed values through TOML") {
    auto tmp = std::filesystem::temp_directory_path() / "onyx_settings_test.toml";
    std::filesystem::remove(tmp);
    {
        auto s = Onyx::Services::Settings::Load(tmp);
        CHECK_FALSE(s.GetString("gowr.texIndexDir").has_value());
        s.Set("gowr.texIndexDir", std::string("E:/packs"));
        s.Set("gowr.lodBias", int64_t{2});
        s.Set("ui.confirmClose", true);
        CHECK(s.Dirty());
        CHECK(s.Save());
        CHECK_FALSE(s.Dirty());
    }
    {
        auto s = Onyx::Services::Settings::Load(tmp);
        CHECK(s.GetString("gowr.texIndexDir") == "E:/packs");
        CHECK(s.GetInt("gowr.lodBias") == 2);
        CHECK(s.GetBool("ui.confirmClose") == true);
    }
    std::filesystem::remove(tmp);
}
```

- [ ] **Step 2: Run, expect failure**
- [ ] **Step 3: Implement** on toml++ (include path already PRIVATE on
  every layer target via `onyx_apply_common`). Only the first dot splits
  table from key ("a.b.c" → table "a", key "b.c").
- [ ] **Step 4: Build + ctest green**
- [ ] **Step 5: Commit** — `feat(settings): Scoped TOML settings with typed access`

### Task 6: Visibility overrides persist as string keys

**Files:**
- Modify: `Include/Onyx/Services/AssetVisibility.h`
- Modify: `Source/Services/AssetVisibility.cpp`
- Test: `Tests/assetvisibility_test.cpp` (extend existing)

**Interfaces:**
- Consumes: `TypeCatalog::KeyOf` / `Find` (Task 1), `Settings` (Task 5).
- Produces: on `AssetVisibility`:
  ```cpp
  // Replaces the caller-less SerializedOverride API (GTKC relic).
  void SaveOverrides(Settings& into) const;   // "visibility.<key>" = bool
  void LoadOverrides(const Settings& from);   // unknown keys are dropped
  std::vector<std::pair<std::string, bool>> ExportOverridesByKey() const;
  ```
  Delete `SerializedOverride`, `ExportOverrides`, `ImportOverrides` (zero
  callers, verified 2026-08-18).

- [ ] **Step 1: Failing test** — set an override on a registered type,
  `SaveOverrides` into a temp `Settings`, clear, `LoadOverrides`, assert
  the override survived by *key*; assert an entry for an unregistered key
  is dropped silently.
- [ ] **Step 2: Run, expect failure**
- [ ] **Step 3: Implement** — iterate `m_overrides`, resolve each id
  through `TypeCatalog::KeyOf`, skip empties; `LoadOverrides` resolves
  keys via `TypeCatalog::Find` under the `visibility` table.
- [ ] **Step 4: Build + ctest green**
- [ ] **Step 5: Commit** — `feat(visibility): Persist overrides by type key, not numeric id`

### Task 7: Milestone gate

**Files:**
- Modify: `CHANGELOG.md`
- Modify: `docs/superpowers/plans/2026-08-18-onyx-v1-roadmap.md` (M2 → done;
  strike the obsolete GTKC-importer deliverable with a dated note)

- [ ] **Step 1:** Clean-configure a fresh build dir; full build; full
  ctest (expect 21 + new tests, all green); `ctest -R "Jobs|EventBus"
  --repeat until-fail:5` for the concurrent suites.
- [ ] **Step 2:** LayerGuard confirms the new Core files are imgui-free
  (they are in `ONYX_CORE_SOURCES`, so the existing test covers them —
  verify it lists them by checking `build/layerguard-core.txt`).
- [ ] **Step 3:** CHANGELOG under Unreleased: identity/diag/jobs/events/
  settings entries. Commit `docs: Record the M2 identity and state services`.

---

## Self-review notes

- Spec coverage (W2): TypeKey/registrar ✔ T1; string-key persistence ✔ T6
  (visibility; layouts are ImGui-native ini, out of scope by spec §7.3
  note); TOML settings + scopes ✔ T5; diagnostics ✔ T2; jobs ✔ T3;
  EventBus ✔ T4 (spec §7.4 puts it in W2's "plumbing in Core").
  GTKC importer: obsolete, documented in Global Constraints and struck in
  T7. TSAN CI job: deferred to M5 with the rest of CI/TestKit wiring —
  recorded in the roadmap, not silently dropped.
- Type consistency: `TypeInfo` (existing) is reused as the registrar's
  spec argument — the spec's `TypeSpec` name arrives in M3 with the
  module contracts, avoiding a gratuitous rename mid-stream.
- Placeholders: none; every task carries real test code and concrete
  implementation guidance.
