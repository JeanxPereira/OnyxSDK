#include "Onyx/Shell/NativeWindow.h"

#if !defined(_WIN32)

namespace Onyx::Shell {

// GLFW's plain path already gives a decorated or an undecorated window; nothing more to install.
void NativeWindow::InstallPlatform() {}

void NativeWindow::RemovePlatform() {}

}

#endif
