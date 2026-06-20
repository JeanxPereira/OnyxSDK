#include <Onyx/Services/AppConfig.h>
#include <Onyx/Services/AssetVisibility.h>
#include <toml++/toml.hpp>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace Onyx::Services {

static AppConfig* s_instance = nullptr;

AppConfig* AppConfig::Get() { return s_instance; }
void AppConfig::SetInstance(AppConfig* cfg) { s_instance = cfg; }

namespace {
// Overwrite each float in `outs` from a TOML array `t[key]` if present.
// Missing entries / wrong types leave the caller's default untouched.
void readFloats(const toml::table& t, std::string_view key,
                std::initializer_list<float*> outs) {
    const auto* arr = t[key].as_array();
    if (!arr) return;
    size_t i = 0;
    for (float* o : outs) {
        if (i < arr->size())
            if (auto v = (*arr)[i].value<double>())
                *o = static_cast<float>(*v);
        ++i;
    }
}

toml::array colorArr(std::initializer_list<float> vals) {
    toml::array a;
    for (float v : vals) a.push_back(static_cast<double>(v));
    return a;
}
} // namespace

// ── Load ───────────────────────────────────────────────────────────────────
// Reads onyx.toml. A missing or invalid file yields all defaults (this is also
// the clean-break path off the legacy binary GTKC format).
AppConfig AppConfig::load(const std::string& path) {
    AppConfig cfg;

    toml::table tbl;
    try {
        tbl = toml::parse_file(path);
    } catch (const toml::parse_error&) {
        return cfg;
    }

    if (const auto* w = tbl["window"].as_table()) {
        cfg.windowX   = static_cast<int>((*w)["x"].value_or<int64_t>(cfg.windowX));
        cfg.windowY   = static_cast<int>((*w)["y"].value_or<int64_t>(cfg.windowY));
        cfg.windowW   = static_cast<int>((*w)["w"].value_or<int64_t>(cfg.windowW));
        cfg.windowH   = static_cast<int>((*w)["h"].value_or<int64_t>(cfg.windowH));
        cfg.maximized = (*w)["maximized"].value_or(cfg.maximized);
    }

    if (const auto* u = tbl["ui"].as_table()) {
        cfg.uiScale            = (*u)["scale"].value_or(cfg.uiScale);
        cfg.fontSize           = (*u)["font_size"].value_or(cfg.fontSize);
        cfg.fontPath           = (*u)["font_path"].value_or(cfg.fontPath);
        cfg.nativeDecorations  = (*u)["native_decorations"].value_or(cfg.nativeDecorations);
        cfg.nativeMenuBar      = (*u)["native_menu_bar"].value_or(cfg.nativeMenuBar);
    }

    if (const auto* th = tbl["theme"].as_table()) {
        cfg.themeMode = static_cast<uint8_t>((*th)["mode"].value_or<int64_t>(cfg.themeMode));
        readFloats(*th, "accent", {&cfg.accentR, &cfg.accentG, &cfg.accentB, &cfg.accentA});
        if (const auto* presets = (*th)["preset"].as_array()) {
            for (const auto& node : *presets) {
                const auto* p = node.as_table();
                if (!p) continue;
                AppConfig::CustomPreset cp;
                std::string name = (*p)["name"].value_or(std::string{});
                std::strncpy(cp.name, name.c_str(), sizeof(cp.name) - 1);
                if (const auto* c = (*p)["color"].as_array()) {
                    if (c->size() > 0) cp.r = static_cast<float>((*c)[0].value_or(0.0));
                    if (c->size() > 1) cp.g = static_cast<float>((*c)[1].value_or(0.0));
                    if (c->size() > 2) cp.b = static_cast<float>((*c)[2].value_or(0.0));
                }
                cfg.customPresets.push_back(cp);
            }
        }
    }

    if (const auto* a = tbl["audio"].as_table())
        cfg.audioVolume = (*a)["volume"].value_or(cfg.audioVolume);

    if (const auto* vp = tbl["viewport"].as_table()) {
        readFloats(*vp, "bg_top",    {&cfg.bgTopR, &cfg.bgTopG, &cfg.bgTopB});
        readFloats(*vp, "bg_bot",    {&cfg.bgBotR, &cfg.bgBotG, &cfg.bgBotB});
        readFloats(*vp, "bone",      {&cfg.boneR, &cfg.boneG, &cfg.boneB});
        readFloats(*vp, "wire",      {&cfg.wireR, &cfg.wireG, &cfg.wireB});
        readFloats(*vp, "matcap",    {&cfg.matcapR, &cfg.matcapG, &cfg.matcapB});
        readFloats(*vp, "grid",      {&cfg.gridR, &cfg.gridG, &cfg.gridB, &cfg.gridA});
        readFloats(*vp, "highlight", {&cfg.hlR, &cfg.hlG, &cfg.hlB, &cfg.hlA});
    }

    if (const auto* cam = tbl["camera"].as_table()) {
        cfg.camFov               = (*cam)["fov"].value_or(cfg.camFov);
        cfg.camNearPlane         = (*cam)["near_plane"].value_or(cfg.camNearPlane);
        cfg.camFarPlane          = (*cam)["far_plane"].value_or(cfg.camFarPlane);
        cfg.camAutoNear          = (*cam)["auto_near"].value_or(cfg.camAutoNear);
        cfg.camAutoFar           = (*cam)["auto_far"].value_or(cfg.camAutoFar);
        cfg.camManualNear        = (*cam)["manual_near"].value_or(cfg.camManualNear);
        cfg.camManualFar         = (*cam)["manual_far"].value_or(cfg.camManualFar);
        cfg.camNearDistanceScale = (*cam)["near_distance_scale"].value_or(cfg.camNearDistanceScale);
        cfg.camFarMargin         = (*cam)["far_margin"].value_or(cfg.camFarMargin);
        cfg.camNearFarRatioMax   = (*cam)["near_far_ratio_max"].value_or(cfg.camNearFarRatioMax);
        cfg.camPanelVisible      = (*cam)["panel_visible"].value_or(cfg.camPanelVisible);
    }

    if (const auto* vis = tbl["visibility"].as_table()) {
        if (const auto* ov = (*vis)["overrides"].as_array()) {
            std::vector<AssetVisibility::SerializedOverride> overrides;
            for (const auto& node : *ov) {
                const auto* o = node.as_table();
                if (!o) continue;
                AssetVisibility::SerializedOverride so;
                so.typeId  = static_cast<uint16_t>((*o)["type"].value_or<int64_t>(0));
                so.visible = (*o)["visible"].value_or(false) ? 1 : 0;
                so._pad    = 0;
                overrides.push_back(so);
            }
            if (!overrides.empty())
                AssetVisibility::Get().ImportOverrides(overrides);
        }
    }

    return cfg;
}

// ── Save ───────────────────────────────────────────────────────────────────
void AppConfig::save(const std::string& path) const {
    toml::table tbl;

    tbl.insert("window", toml::table{
        {"x", static_cast<int64_t>(windowX)},
        {"y", static_cast<int64_t>(windowY)},
        {"w", static_cast<int64_t>(windowW)},
        {"h", static_cast<int64_t>(windowH)},
        {"maximized", maximized},
    });

    tbl.insert("ui", toml::table{
        {"scale", static_cast<double>(uiScale)},
        {"font_size", static_cast<double>(fontSize)},
        {"font_path", fontPath},
        {"native_decorations", nativeDecorations},
        {"native_menu_bar", nativeMenuBar},
    });

    toml::table theme{
        {"mode", static_cast<int64_t>(themeMode)},
        {"accent", colorArr({accentR, accentG, accentB, accentA})},
    };
    if (!customPresets.empty()) {
        toml::array presets;
        for (const auto& cp : customPresets) {
            char nameBuf[33];
            std::memcpy(nameBuf, cp.name, 32);
            nameBuf[32] = '\0';
            presets.push_back(toml::table{
                {"name", std::string(nameBuf)},
                {"color", colorArr({cp.r, cp.g, cp.b})},
            });
        }
        theme.insert("preset", std::move(presets));
    }
    tbl.insert("theme", std::move(theme));

    tbl.insert("audio", toml::table{{"volume", static_cast<double>(audioVolume)}});

    tbl.insert("viewport", toml::table{
        {"bg_top",    colorArr({bgTopR, bgTopG, bgTopB})},
        {"bg_bot",    colorArr({bgBotR, bgBotG, bgBotB})},
        {"bone",      colorArr({boneR, boneG, boneB})},
        {"wire",      colorArr({wireR, wireG, wireB})},
        {"matcap",    colorArr({matcapR, matcapG, matcapB})},
        {"grid",      colorArr({gridR, gridG, gridB, gridA})},
        {"highlight", colorArr({hlR, hlG, hlB, hlA})},
    });

    tbl.insert("camera", toml::table{
        {"fov", static_cast<double>(camFov)},
        {"near_plane", static_cast<double>(camNearPlane)},
        {"far_plane", static_cast<double>(camFarPlane)},
        {"auto_near", camAutoNear},
        {"auto_far", camAutoFar},
        {"manual_near", static_cast<double>(camManualNear)},
        {"manual_far", static_cast<double>(camManualFar)},
        {"near_distance_scale", static_cast<double>(camNearDistanceScale)},
        {"far_margin", static_cast<double>(camFarMargin)},
        {"near_far_ratio_max", static_cast<double>(camNearFarRatioMax)},
        {"panel_visible", camPanelVisible},
    });

    auto overrides = AssetVisibility::Get().ExportOverrides();
    if (!overrides.empty()) {
        toml::array ov;
        for (const auto& so : overrides) {
            ov.push_back(toml::table{
                {"type", static_cast<int64_t>(so.typeId)},
                {"visible", so.visible != 0},
            });
        }
        tbl.insert("visibility", toml::table{{"overrides", std::move(ov)}});
    }

    std::ofstream f(path, std::ios::binary);
    if (!f.is_open()) return;
    f << "# OnyxSDK configuration (TOML). Window/dock layout lives in imgui.ini.\n";
    f << tbl << "\n";
}

} // namespace Onyx::Services
