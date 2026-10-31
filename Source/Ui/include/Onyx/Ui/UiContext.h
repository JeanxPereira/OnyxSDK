#pragma once

#include <Onyx/Core/Modules/Workspace.h>

namespace Onyx::Gpu {
class Device;
class Frame;
}

namespace Onyx::Ui {

class TexturePool;
class Theme;
class CommandRegistry;
class ViewerRegistry;
class PanelHost;
class FrameClock;

struct UiContext {
    Modules::Workspace& workspace;
    Gpu::Device* gpu;
    Gpu::Frame* frame;
    TexturePool& textures;
    Theme& theme;
    CommandRegistry& commands;
    ViewerRegistry& viewers;
    PanelHost& panels;
    FrameClock& clock;
    float dt;
};

}
