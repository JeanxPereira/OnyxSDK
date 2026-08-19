// Same stub, same reasoning, as Tools/OnyxOracle/AppConfigStub.cpp (T14
// discovered the identical gap onyx-oracle already worked around, the
// first time this executable linked Onyx::Render without also linking
// Onyx::Shell): Onyx::Rendering::ShaderManager::GenerateMatcapTexture()
// (and, transitively through it, SceneRenderer::Render()) reads
// Onyx::Services::AppConfig::Get() for an optional matcap tint override,
// falling back to its own defaults when it returns null. AppConfig.h is
// deliberately imgui-free so Render can name it, but AppConfig.cpp -- the
// translation unit that actually defines Get() -- lives in Onyx_Shell
// because it pulls in Appearance.h/ImVec4 to persist theme state.
// onyxbox-cli links Onyx::Render (see Examples/OnyxCli/CMakeLists.txt's own
// comment for why) but never Onyx::Shell, so nothing else supplies this
// definition. This stub always returns null, exactly like the real Get()
// does before App registers an instance -- CmdRender always renders in
// ShadingMode::Solid (Render.cpp), which never reaches the matcap texture
// path anyway (ShaderManager.cpp gates that behind ShadingMode::Matcap),
// so the returned null is never even observed today. Do not add Onyx_Shell
// as a link dependency to satisfy this instead -- that would drag imgui
// into a headless CLI built to run without a UI.

#include <Onyx/Services/AppConfig.h>

namespace Onyx::Services {

AppConfig* AppConfig::Get() { return nullptr; }

} // namespace Onyx::Services
