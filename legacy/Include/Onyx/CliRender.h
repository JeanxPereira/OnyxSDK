#pragma once
// Onyx SDK — headless CLI render umbrella (sibling to Onyx.h).
//
// LINK Onyx::CliRender to use anything declared here. Onyx_CliRender is
// not among the targets Onyx::Onyx links (Onyx_Core, Onyx_Render,
// Onyx_Shell), so <Onyx/Onyx.h> does not name Cli/Render.h -- see that
// file's inclusion rule, clause (1). This target exists at all because of
// the public-surface audit's one blocking gap (G1): Cli/Render.h was a
// public header whose CmdRender shipped in no library, and the fix was to
// build one, not to hide the header.
//
// The rest of the CLI vocabulary -- Onyx::Cli::Run, CmdProbe/CmdList/
// CmdExtract/CmdDecode, the exit-code constants, the RenderFn hook type --
// ships in Onyx_Core and IS named by <Onyx/Onyx.h> (Cli/Commands.h). Only
// `render`'s implementation lives out here, because it needs the renderer
// and Onyx_Core must not link Onyx_Render (that direction is a cycle --
// Cli/Render.h's own top comment walks through it). A composition root
// that wants `render` links both and passes Onyx::Cli::CmdRender as
// Run()'s RenderFn hook, exactly as Examples/OnyxCli/Main.cpp does:
//
//   target_link_libraries(MyCli PRIVATE Onyx::Core Onyx::CliRender)
//   #include <Onyx/Onyx.h>       // Onyx::Cli::Run
//   #include <Onyx/CliRender.h>  // Onyx::Cli::CmdRender
//
// Onyx_CliRender links Onyx_Core + Onyx_Render PUBLIC, so linking it is
// enough; naming Onyx::Render as well is belt-and-suspenders.
//
// Third-party dependencies to compile a TU that includes ONLY this header:
// none beyond what Onyx/Cli/Commands.h already needs (glm, via
// Modules::Workspace). No Vulkan header is reachable from here -- CmdRender
// takes a path and view names, not a VkDevice.
#include <Onyx/Cli/Render.h>
