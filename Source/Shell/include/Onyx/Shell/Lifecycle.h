#pragma once

#include <filesystem>
#include <span>

namespace Onyx::Shell {

class Shell;

// Hooks a host installs with Shell::SetLifecycle. All run on the main thread, between frames.
class LifecycleDelegate {
public:
    virtual ~LifecycleDelegate();

    // Return false to keep the window open after a close request.
    virtual bool WillClose(Shell&) { return true; }
    // Called after the Shell has already opened the dropped files.
    virtual void FilesDropped(Shell&, std::span<const std::filesystem::path>) {}
    // Called once, after the first frame was presented.
    virtual void DidStart(Shell&) {}
};

}
