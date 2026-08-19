# -- OnyxCli -- headless probe/list/extract/decode/render CLI over OnyxBox -
# Proves Task 6's Onyx::Cli::Run dispatcher against the synthetic OnyxBox
# module (probe/list/extract/decode: no UI, no GPU -- Onyx_Core +
# Onyx_ExampleBox only), PLUS Task 14's `render` command, which needs a
# headless Vulkan renderer (Onyx::Render). Render.cpp -- the CmdRender
# implementation -- lives right here in Examples/OnyxCli/ (NOT Source/Cli/,
# despite Include/Onyx/Cli/Render.h's own location) and is added directly
# to THIS target's own sources rather than to Onyx_Core: Onyx_Core is
# documented "no UI, no GPU" and Onyx_Render already links Onyx_Core
# PUBLIC (root CMakeLists.txt), so giving Onyx_Core a reverse dependency on
# Onyx_Render would be a real link cycle, not just a layering smell.
# Concretely forced here, not just architecturally motivated: root
# CMakeLists.txt's completeness check globs Source/*.cpp and requires every
# match be listed in exactly one of the four ONYX_*_SOURCES layer lists
# (Core/Media/Shell/Render -- Task 11 folded the fifth, RenderVk, into
# Render when it deleted the GL renderer that used to hold that name) -- a
# file compiled straight into an executable, claimed by none of them,
# fails that configure-time check the moment it lives under Source/.
# Examples/ is outside the glob, which is exactly the boundary this file
# needs. This executable is the one place Onyx_Core and Onyx::Render may
# meet -- see Include/Onyx/Cli/Render.h's top comment for the full writeup.
add_executable(onyxbox-cli
    ${CMAKE_CURRENT_SOURCE_DIR}/Main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Render.cpp
)
target_include_directories(onyxbox-cli PRIVATE ${CMAKE_SOURCE_DIR}/third_party/stb)
# Task 11 closed the link-completeness gap this comment used to document:
# onyxbox-cli was the FIRST target in the tree to link the Vulkan renderer
# (Onyx::RenderVk, since renamed Onyx::Render -- Task 11's own mechanical
# rename commit) without also linking the GL one (Onyx::Render at the
# time, deleted along with the rest of the GL renderer in the same task),
# which exposed that the Vulkan renderer was not actually link-complete
# standalone -- Source/RenderVk/SceneRendererVk.cpp called Onyx::Rendering::
# SceneRenderer::ResolveRoleIndices (namespace Onyx::Rendering was, and
# still is, the GL-and-then-shared renderer namespace; SceneRenderer was
# the GL class), whose only DEFINITION lived in the GL-only
# Source/Rendering/SceneRenderer.cpp. That method (now a free function,
# Onyx::Rendering::ResolveRoleIndices) moved to Source/Rendering/
# RenderBatch.cpp -- part of the renderer's own source list -- and
# Onyx::Rendering::Camera's own AppConfig::Get() call (the other half of
# why this executable, and Tools/OnyxOracle, each needed their own
# AppConfigStub.cpp) moved out to Onyx::Viewers::Viewport3D, the one Shell
# class that actually owns both a Camera and AppConfig. The renderer links
# standalone now with zero stubs, and is the only renderer left in the
# tree, so there is nothing left to link here but Onyx::Render itself.
target_link_libraries(onyxbox-cli PRIVATE Onyx_Core Onyx_ExampleBox Onyx::Render)

# OnyxCliRender (Task 14): device-gated (SKIP 77, same convention as every
# onyx-oracle Vk* ctest) -- writes a mesh fixture via `write-render-fixture`,
# renders it twice independently through `render`, and asserts both PNGs
# exist and are byte-identical. See RenderTest.cmake's own top comment for
# why "non-uniform" is enforced inside CmdRender itself (Source/Cli/
# Render.cpp) rather than re-checked here.
add_test(NAME OnyxCliRender
    COMMAND ${CMAKE_COMMAND}
      -DCLI=$<TARGET_FILE:onyxbox-cli>
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render-test
      -P ${CMAKE_CURRENT_SOURCE_DIR}/RenderTest.cmake)
set_tests_properties(OnyxCliRender PROPERTIES SKIP_RETURN_CODE 77 LABELS "render")
