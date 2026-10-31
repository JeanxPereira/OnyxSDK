#pragma once

#include <Onyx/Core/Result.h>
#include <Onyx/Ui/Panel.h>
#include <Onyx/Ui/Selection.h>
#include <Onyx/Ui/UiContext.h>

#include <cstdint>
#include <functional>
#include <memory>

// Error codes (Result<void>::error().code):
//   ui.viewer.none    no registered viewer routes the entry's type
//   ui.open.entry     the selection names no document or no entry in it
//   ui.open.range     the entry's byte range names a file the document does not hold
namespace Onyx::Ui {

// Decodes run on this job lane; the continuation that builds the view runs inside Jobs().Pump().
inline constexpr uint64_t kDecodeLane = 0;

// Routes the selected entry to a viewer. A payload kind (image, text, scene, audio, table) is decoded
// on the job queue and `place` is called with the finished view from Jobs().Pump(); a bare entry
// (raw, unknown) is placed before OpenEntry returns with nothing read: the hex view streams its rows
// through a Vfs::Reader, so no blob is ever loaded whole. A decode that fails, or a decoded payload the
// viewer makes no view of, reports ui.open.decode (naming the entry and its kind) on the document and
// never calls `place`. The workspace must outlive the pending continuation.
Result<void> OpenEntry(UiContext& ui, const Selection& selection,
                       std::function<void(std::unique_ptr<IDocumentView>)> place);

}
