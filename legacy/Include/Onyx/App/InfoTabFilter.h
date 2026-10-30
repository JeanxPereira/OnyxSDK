#pragma once

// Pure diag<->entry association for InfoTab's Workspace-bus leg (M3b Task
// 5). Split out of InfoTab.cpp so it is testable without ImGui.
//
// Diags do not yet carry a NodePath (or any other reference back to the
// specific tree node they describe) -- ContainerContext::diags is a
// per-document sink, not a per-entry one (Workspace.h). Substring-matching
// the diag's message against the entry's name is the only association
// available today. It is an honest approximation, not a real link: a diag
// whose message happens to mention an unrelated entry that shares a
// substring with the selected one's name will over-match, and a diag
// that describes an entry without naming it (or names it differently,
// e.g. a hash instead of a display name) will under-match. This stays
// until diags are extended to carry a NodePath of their own.

#include <string_view>

namespace Onyx::Services { struct Diag; }

namespace Onyx::App {

// True if `diag.message` contains `entryName` as a substring. False for an
// empty `entryName` (an empty needle would otherwise "match" everything).
bool DiagMentionsEntry(const Onyx::Services::Diag& diag, std::string_view entryName);

} // namespace Onyx::App
