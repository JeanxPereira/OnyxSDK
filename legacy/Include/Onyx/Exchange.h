#pragma once
// Onyx SDK — exchange umbrella (sibling to Onyx.h).
//
// LINK Onyx::Exchange to use anything declared here. That is the whole
// reason this header exists apart from <Onyx/Onyx.h>: the main umbrella
// names only headers whose symbols ship in a target Onyx::Onyx itself
// links (Onyx_Core, Onyx_Render, Onyx_Shell -- root CMakeLists.txt's
// `target_link_libraries(Onyx INTERFACE ...)`), and Onyx_Exchange is not
// one of them. Declaring ExportSceneData from the main umbrella would
// hand a consumer who linked the documented aggregate a declaration that
// compiles and then fails at link with LNK2019 -- see <Onyx/Onyx.h>'s own
// inclusion rule, clause (1).
//
// Onyx_Exchange links Onyx_Core ALONE, never Onyx_Render: Onyx_Render
// already links Onyx_Core PUBLIC, so the reverse link would be a real
// CMake cycle (Include/Onyx/Cli/Render.h documents the identical shape
// for CmdRender). So a consumer who wants glTF export out of a headless
// tool pays for no renderer at all:
//
//   target_link_libraries(MyTool PRIVATE Onyx::Core Onyx::Exchange)
//   #include <Onyx/Exchange.h>
//
// Third-party dependencies to compile a TU that includes ONLY this header:
// none beyond glm (reached through Parsers/SceneData). cgltf is an
// implementation detail of Onyx_Exchange and appears in no public header.
#include <Onyx/Exchange/GltfExport.h>
