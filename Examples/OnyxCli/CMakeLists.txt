# -- OnyxCli -- headless probe/list/extract/decode/render CLI over OnyxBox -
# Proves Task 6's Onyx::Cli::Run dispatcher against the synthetic OnyxBox
# module (probe/list/extract/decode: no UI, no GPU -- Onyx_Core +
# Onyx_ExampleBox only), PLUS Task 14's `render` command, which needs a
# headless Vulkan renderer (Onyx::RenderVk). Render.cpp -- the CmdRender
# implementation -- lives right here in Examples/OnyxCli/ (NOT Source/Cli/,
# despite Include/Onyx/Cli/Render.h's own location) and is added directly
# to THIS target's own sources rather than to Onyx_Core: Onyx_Core is
# documented "no UI, no GPU" and Onyx_RenderVk already links Onyx_Core
# PUBLIC (root CMakeLists.txt), so giving Onyx_Core a reverse dependency on
# Onyx_RenderVk would be a real link cycle, not just a layering smell.
# Concretely forced here, not just architecturally motivated: root
# CMakeLists.txt's completeness check globs Source/*.cpp and requires every
# match be listed in exactly one of the five ONYX_*_SOURCES layer lists
# (Core/Render/Media/Shell/RenderVk) -- a file compiled straight into an
# executable, claimed by none of them, fails that configure-time check the
# moment it lives under Source/. Examples/ is outside the glob, which is
# exactly the boundary this file needs. This executable is the one place
# Onyx_Core and Onyx::RenderVk may meet -- see Include/Onyx/Cli/Render.h's
# top comment for the full writeup.
add_executable(onyxbox-cli
    ${CMAKE_CURRENT_SOURCE_DIR}/Main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Render.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AppConfigStub.cpp
)
target_include_directories(onyxbox-cli PRIVATE ${CMAKE_SOURCE_DIR}/third_party/stb)
# Onyx::Render (GL) is linked here too, alongside Onyx::RenderVk, even
# though CmdRender (Render.cpp) never issues a GL call: this is the FIRST
# target in the tree to link Onyx::RenderVk without also linking Onyx::
# Render, and doing so exposed that Onyx_RenderVk is not actually
# link-complete standalone yet -- Source/RenderVk/SceneRendererVk.cpp calls
# Onyx::Rendering::SceneRenderer::ResolveRoleIndices (declared in Onyx_Core-
# visible SceneRenderer.h, which Onyx_RenderVk already includes for
# RenderBatch/ShadingMode -- see SceneRendererVk.h's own top comment), but
# that static method's DEFINITION lives only in Source/Rendering/
# SceneRenderer.cpp, which is Onyx_Render-only. Every other composition
# root that links Onyx::RenderVk today (onyx-oracle, onyx_tests,
# MinimalViewer) also links Onyx::Render (or Onyx::Onyx, which includes
# it), so none of them ever hit this at link time. Closing that gap for
# real (moving/duplicating ResolveRoleIndices the way JointPalette.cpp
# already is, per that source's own CMakeLists.txt comment) is a separate,
# already-named carried task (the M4 plan's "link-completeness (T11)") --
# out of this task's scope. Linking Onyx::Render here too is the same
# workaround every existing consumer already uses, not a new pattern.
target_link_libraries(onyxbox-cli PRIVATE Onyx_Core Onyx_ExampleBox Onyx::Render Onyx::RenderVk)

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
