#pragma once

// The pure "which viewer opens this type" decision (M3b Task 4).
//
// Lives under App/ (Shell) because the Shell is the only consumer -- the
// document browser needs to know which tab to open on a double-click --
// but the function itself touches neither ImGui nor any GL/GLFW header,
// so it links cleanly into the CORE-only test target and is testable
// without standing up a window. LayerGuard does not scan App/ sources
// (only Core and Render are guarded -- see the root CMakeLists.txt), so
// staying imgui-free here is a deliberate discipline, not an enforced one.
//
// Priority: a type can be registered for more than one capability (e.g. a
// mesh entry that also carries an embedded preview image). Scene wins
// over Image, which wins over Text, on the theory that the richest viewer
// available is always the one worth opening -- a 3D preview subsumes a
// flat image preview, which subsumes a text dump.

#include <Onyx/Types/TypeId.h>
#include <cstdint>

namespace Onyx::Modules { class DecoderRegistry; }

namespace Onyx::App {

enum class ViewerKind : uint8_t { None, Image, Text, Scene };

// Priority: Scene > Image > Text. None when the registry has no decoder
// capability at all for `typeId` -- including an invalid/unregistered
// TypeId, which simply matches nothing in the registry's lookup.
ViewerKind RouteForType(const Onyx::Modules::DecoderRegistry& decoders, Onyx::Types::TypeId typeId);

} // namespace Onyx::App
