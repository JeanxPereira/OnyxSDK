onyx_add_layer(Render PUBLIC_DEPS Onyx::Gpu)
onyx_add_spirv(Onyx_Render
    Shaders/Scene.vert Shaders/Scene.frag
    Shaders/Grid.vert Shaders/Grid.frag
    Shaders/Background.vert Shaders/Background.frag
    Shaders/Overlay.vert Shaders/Overlay.frag)
