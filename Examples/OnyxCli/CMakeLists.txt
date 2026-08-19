# -- OnyxCli -- headless probe/list/extract/decode/render CLI over OnyxBox -
# Proves Task 6's Onyx::Cli::Run dispatcher against the synthetic OnyxBox
# module (probe/list/extract/decode: no UI, no GPU -- Onyx_Core +
# Onyx_ExampleBox only), PLUS `render` (Task 14, moved into the SDK proper
# at M5 Task 6). `render`'s implementation, CmdRender, no longer lives in
# this directory at all -- it moved to Source/Cli/Render.cpp, compiled into
# the shipped Onyx_CliRender library (root CMakeLists.txt), fixing the
# public-surface-audit finding (G1) that the public Include/Onyx/Cli/
# Render.h declared a symbol shipping in no library. This executable is
# simply a CONSUMER of that library now, same as it consumes Onyx_Core --
# no Vulkan/renderer source lives under Examples/ any more. See
# Include/Onyx/Cli/Render.h's top comment for the full history and the
# link-cycle reasoning (Onyx_Core cannot link Onyx_Render or
# Onyx_CliRender directly; Onyx_CliRender links Onyx_Core + Onyx_Render
# PUBLIC, the other direction).
#
# `render`'s argv parsing (--out/--width/--height/--views/--strict) also
# moved into the SDK: Onyx::Cli::Run() (Source/Cli/Commands.cpp) now
# recognizes "render" itself, reaching CmdRender through an injected
# RenderFn hook (Include/Onyx/Cli/Commands.h's own doc comment) rather
# than Main.cpp special-casing the verb before falling through to Run() --
# Main.cpp below just passes `Onyx::Cli::CmdRender` itself as that hook,
# since Onyx::CliRender is a library it links like any other.
#
# Gltf.cpp (M5 Task 5) -- MakeGltfExportFn, the SceneExportFn-shaped bridge
# `decode --to gltf` needs between Onyx_Core (Commands.cpp, which cannot
# link Onyx_Exchange for the identical reason it cannot link Onyx::Render
# -- see Include/Onyx/Cli/Gltf.h's top comment) and Onyx::Exchange. This is
# the one CmdRender-adjacent case that still uses the injected-callback
# shape end to end (Include/Onyx/Cli/Render.h's own top comment explains
# why `render` itself does not): unlike CmdRender, `Onyx::Exchange::
# ExportSceneData` is not the thing being shipped here -- the SceneExportFn
# TYPE (Commands.h) is Onyx_Core's public contract, and any composition
# root that links Onyx::Exchange can bind its own hook the same way this
# one does, with no SDK-side "the symbol itself must be linkable" claim
# like Render.h makes for CmdRender.
add_executable(onyxbox-cli
    ${CMAKE_CURRENT_SOURCE_DIR}/Main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Gltf.cpp
)
# No third_party/stb include dir needed here any more -- WritePng's own
# stb_image_write.h use moved with Render.cpp into Onyx_CliRender, which
# declares that include dir on itself (root CMakeLists.txt).
# Onyx::CliRender pulls in Onyx::Render (and Onyx::Core) PUBLIC on its own
# (root CMakeLists.txt), so listing Onyx_Core/Onyx::Render here too is
# belt-and-suspenders, not strictly required -- kept explicit for the same
# reason this target has always spelled out its own link set in full.
target_link_libraries(onyxbox-cli PRIVATE
    Onyx_Core
    Onyx_ExampleBox
    Onyx::CliRender
    Onyx::Exchange)

# OnyxCliRender (Task 14): device-gated (SKIP 77, same convention as every
# onyx-oracle Vk* ctest) -- writes a mesh fixture via `write-render-fixture`,
# renders it twice independently through `render`, and asserts both PNGs
# exist and are byte-identical. Regression bar for M5 Task 6: this must
# keep passing completely unchanged -- `render`'s argv now flows through
# Onyx::Cli::Run() instead of Main.cpp's own parsing, and the default
# (no --views) camera is still exactly the "iso" view's 45deg-yaw/15deg-
# pitch orbit, so the two runs' PNGs stay byte-identical to before this
# task. See RenderTest.cmake's own top comment for why "non-uniform" is
# enforced inside CmdRender itself (Source/Cli/Render.cpp) rather than
# re-checked here.
add_test(NAME OnyxCliRender
    COMMAND ${CMAKE_COMMAND}
      -DCLI=$<TARGET_FILE:onyxbox-cli>
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render-test
      -P ${CMAKE_CURRENT_SOURCE_DIR}/RenderTest.cmake)
set_tests_properties(OnyxCliRender PROPERTIES SKIP_RETURN_CODE 77 LABELS "render")

# OnyxCliRenderViews (M5 Task 6, spec §11's `--views` flag): renders all
# six kCanonicalViews (Include/Onyx/Cli/Commands.h) in one `render` call
# and asserts all six PNGs exist. See RenderViewsTest.cmake's own top
# comment.
add_test(NAME OnyxCliRenderViews
    COMMAND ${CMAKE_COMMAND}
      -DCLI=$<TARGET_FILE:onyxbox-cli>
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render-views-test
      -P ${CMAKE_CURRENT_SOURCE_DIR}/RenderViewsTest.cmake)
set_tests_properties(OnyxCliRenderViews PROPERTIES SKIP_RETURN_CODE 77 LABELS "render")

# OnyxCliRenderStrict (M5 Task 6, spec §11's `--strict` flag): renders a
# fixture that carries a document-level Error diag (a corrupt sibling
# entry, same shape Tests/cli_test.cpp's WriteSampleBox uses for
# `decode --strict`'s own test) and asserts `--strict` turns that into a
# nonzero exit while the render itself still succeeds and writes a PNG.
# See RenderStrictTest.cmake's own top comment.
add_test(NAME OnyxCliRenderStrict
    COMMAND ${CMAKE_COMMAND}
      -DCLI=$<TARGET_FILE:onyxbox-cli>
      -DWORK=${CMAKE_CURRENT_BINARY_DIR}/render-strict-test
      -P ${CMAKE_CURRENT_SOURCE_DIR}/RenderStrictTest.cmake)
set_tests_properties(OnyxCliRenderStrict PROPERTIES SKIP_RETURN_CODE 77 LABELS "render")
