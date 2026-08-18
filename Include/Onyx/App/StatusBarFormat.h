#pragma once

// Pure formatting for the Workspace-driven status line (M3b Task 5).
// Split out of StatusBar.cpp so it is testable without standing up ImGui
// or a Workspace -- same rationale as ViewerRouting.h's file comment.

#include <cstddef>
#include <string>
#include <string_view>

namespace Onyx::App {

// While a document's async parse hasn't finished: "opening <filename>:
// <label> (NN%)". `fraction` is clamped to [0, 1] and rounded to the
// nearest whole percent; an empty `label` (progress hasn't Step()ped yet)
// still renders, just with nothing between the colon and the percentage.
std::string FormatOpeningLine(std::string_view filename, float fraction, std::string_view label);

// Once every open document is ready: "N docs, M entries, K errors" with
// each noun singular/plural-matched to its count.
std::string FormatSummaryLine(size_t docCount, size_t entryCount, size_t errorCount);

} // namespace Onyx::App
