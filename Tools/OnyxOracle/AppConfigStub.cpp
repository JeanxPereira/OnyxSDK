// Onyx::Rendering::ShaderManager::GenerateMatcapTexture() reads
// Onyx::Services::AppConfig::Get() for an optional matcap tint override, and
// falls back to its own defaults when it returns null. AppConfig.h is
// deliberately imgui-free so Render can name it (see the header's own
// comment), but AppConfig.cpp — the translation unit that defines Get() —
// lives in Onyx_Shell because it pulls in Appearance.h/ImVec4 to persist
// theme state. Onyx-oracle links only Onyx::Render, never Shell, so nothing
// supplies that definition; this stub is the one Onyx-oracle needs, always
// returning null exactly like the real Get() does before App registers an
// instance. Do not add Onyx_Shell as a link dependency to satisfy this
// instead — that would drag imgui into a headless tool built to render
// without a UI.

#include <Onyx/Services/AppConfig.h>

namespace Onyx::Services {

AppConfig* AppConfig::Get() { return nullptr; }

} // namespace Onyx::Services
