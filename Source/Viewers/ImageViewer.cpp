#include <Onyx/Viewers/ImageViewer.h>
#include <Onyx/App/TexturePool.h>
#include <Onyx/RenderVk/VkContext.h>
#include <Onyx/Fonts/SFSymbols.h>
#include <Onyx/Services/Logger.h>
#include <Onyx/Services/ThemeManager.h>
#include <Onyx/App/Widgets.h>
#include <algorithm>
#include <cmath>
#include <imgui.h>


namespace Onyx::Viewers {

ImageViewer::ImageViewer(const std::string &name,
                         std::unique_ptr<Parsers::TextureData> texture)
    : m_name(name), m_texture(std::move(texture)) {
  if (m_texture && m_texture->IsValid()) {
    UploadToGPU();
  }
}

ImageViewer::~ImageViewer() {
  // m_texPool's own destructor carries the shutdown-order guard (see
  // TexturePool.h) -- nothing extra needed here.
}

std::string ImageViewer::GetName() const { return m_name; }

void ImageViewer::UploadToGPU() {
  if (!m_texture || !m_texture->IsValid())
    return;

  // Compressed (block-compressed, GL-swizzle-driven) textures: verified
  // dead code before this task started (grep across the repo -- no loader
  // anywhere ever sets TextureData::isCompressed = true; Tests/
  // oracle_corpus_test.cpp even asserts isCompressed == false on its own
  // corpus). TextureData.h's own doc comment says the struct is "always
  // RGBA8 output" -- this branch was the one place that promise didn't
  // hold, for a GL-only optimization (hardware BC1/4/5 sampling +
  // glTexParameteriv swizzle) with nothing left in this milestone to
  // exercise it. Rather than port block-compressed upload + a runtime
  // channel-swizzle to Vulkan for a path nothing calls, this stays a
  // logged no-op -- disclosed in task-10-report.md, not silently dropped.
  if (m_texture->isCompressed) {
    LOG_WARN("[ImageViewer] '%s': compressed texture upload not ported to Vulkan "
             "(dead code path, no producer sets isCompressed=true) -- not displayed",
             m_name.c_str());
    return;
  }

  Onyx::Rendering::VkContext* ctx = Onyx::Rendering::GetGlobalContext();
  if (!ctx) {
    LOG_ERR("[ImageViewer] '%s': no live VkContext -- cannot upload texture", m_name.c_str());
    return;
  }
  if (!m_texPool) {
    m_texPool = std::make_unique<Onyx::App::TexturePool>(*ctx);
  }

  std::string err;
  m_texId = m_texPool->Create(m_texture->width, m_texture->height, m_texture->pixels.data(), err);
  if (m_texId == ImTextureID_Invalid) {
    LOG_ERR("[ImageViewer] '%s': TexturePool::Create failed: %s", m_name.c_str(), err.c_str());
    return;
  }

  LOG_INFO("[ImageViewer] '%s': uploaded %ux%u, imtex=0x%llx", m_name.c_str(),
           m_texture->width, m_texture->height, (unsigned long long)m_texId);
}

void ImageViewer::ApplyAlphaToggle() {
  if (!m_texture || m_texId == ImTextureID_Invalid || !m_texPool) return;

  const size_t pixelCount = static_cast<size_t>(m_texture->width) * m_texture->height;
  if (m_texture->pixels.size() < pixelCount * 4) return;

  std::vector<uint8_t> display(pixelCount * 4);
  if (m_showAlpha) {
    // (A, A, A, 255) -- alpha channel shown as opaque grayscale.
    for (size_t i = 0; i < pixelCount; ++i) {
      const uint8_t a = m_texture->pixels[i * 4 + 3];
      display[i * 4 + 0] = a;
      display[i * 4 + 1] = a;
      display[i * 4 + 2] = a;
      display[i * 4 + 3] = 255;
    }
  } else {
    display = m_texture->pixels; // restore the original RGBA content
    if (display.size() > pixelCount * 4) display.resize(pixelCount * 4);
  }

  std::string err;
  if (!m_texPool->Update(m_texId, display.data(), err)) {
    LOG_ERR("[ImageViewer] '%s': TexturePool::Update (alpha toggle) failed: %s",
            m_name.c_str(), err.c_str());
  }
}

void ImageViewer::ZoomToAnchored(float newZoom, ImVec2 anchorScreen) {
  newZoom = std::clamp(newZoom, 0.125f, 16.0f);
  if (newZoom == m_zoomTarget) return;
  // Image-local coords of the anchor at the current (target) pan.
  const ImVec2 local(anchorScreen.x - m_panTarget.x,
                     anchorScreen.y - m_panTarget.y);
  const float  scale = newZoom / m_zoomTarget;
  m_panTarget.x = anchorScreen.x - local.x * scale;
  m_panTarget.y = anchorScreen.y - local.y * scale;
  m_zoomTarget  = newZoom;
}

void ImageViewer::Draw() {
  if (!m_texture || m_texId == ImTextureID_Invalid) {
    ImGui::TextDisabled("No texture data");
    return;
  }

  const float texW = static_cast<float>(m_texture->width);
  const float texH = static_cast<float>(m_texture->height);

  // ── Toolbar ──────────────────────────────────────────────────────────
  ImGui::PushStyleColor(ImGuiCol_Button, Onyx::Theme::ToolbarButton());
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Onyx::Theme::ToolbarButtonHover());

  ImGui::TextDisabled("%ux%u", m_texture->width, m_texture->height);
  ImGui::SameLine();

  // Defer button actions until we know `avail` (canvas size) — the viewport
  // center anchor needs it. Bool flags carry the intent across.
  const bool zoomInClicked  = Onyx::App::Widgets::SmallButton(ICON_SF_PLUS_MAGNIFYINGGLASS);
  ImGui::SameLine();
  const bool zoomOutClicked = Onyx::App::Widgets::SmallButton(ICON_SF_MINUS_MAGNIFYINGGLASS);
  ImGui::SameLine();
  const bool oneToOneClicked = Onyx::App::Widgets::SmallButton("1:1");
  ImGui::SameLine();
  const bool fitClicked = Onyx::App::Widgets::SmallButton("Fit");
  ImGui::SameLine();

  // Alpha toggle — show alpha channel as grayscale
  bool prevAlpha = m_showAlpha;
  ImGui::Checkbox("Alpha", &m_showAlpha);
  const bool alphaChanged = (m_showAlpha != prevAlpha);

  ImGui::PopStyleColor(2);
  ImGui::Separator();

  // ── Canvas area (everything below toolbar) ───────────────────────────
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  if (avail.x <= 1.0f || avail.y <= 1.0f) return;

  const ImVec2 origin = ImGui::GetCursorScreenPos();

  // Helpers shared by buttons / first-frame init.
  auto fitZoomFor = [&]() {
    return std::min(avail.x / texW, avail.y / texH);
  };
  auto centerForZoom = [&](float z) {
    return ImVec2((avail.x - texW * z) * 0.5f,
                  (avail.y - texH * z) * 0.5f);
  };
  // Anchor in canvas-local coords (matches m_panTarget convention).
  const ImVec2 viewportCenter(avail.x * 0.5f, avail.y * 0.5f);

  // ── First-frame init: open in Fit mode (no lerp; nothing to ease from) ──
  if (!m_initialFitDone) {
    m_zoomTarget = fitZoomFor();
    m_panTarget  = centerForZoom(m_zoomTarget);
    m_zoom = m_zoomTarget;
    m_pan  = m_panTarget;
    m_initialFitDone = true;
  }

  // ── Toolbar actions (lerp toward target — same path as wheel) ────────
  if (zoomInClicked)   ZoomToAnchored(m_zoomTarget * 1.5f, viewportCenter);
  if (zoomOutClicked)  ZoomToAnchored(m_zoomTarget / 1.5f, viewportCenter);
  if (oneToOneClicked) ZoomToAnchored(1.0f,                 viewportCenter);
  if (fitClicked) {
    m_zoomTarget = fitZoomFor();
    m_panTarget  = centerForZoom(m_zoomTarget);
  }

  ImGui::InvisibleButton("##texcanvas", avail,
                         ImGuiButtonFlags_MouseButtonLeft |
                         ImGuiButtonFlags_MouseButtonMiddle);
  const bool hovered = ImGui::IsItemHovered();
  const bool active  = ImGui::IsItemActive();
  ImGuiIO& io = ImGui::GetIO();

  // Cursor-anchored wheel zoom — same unified helper.
  if (hovered && io.MouseWheel != 0.0f) {
    const float factor = std::pow(1.15f, io.MouseWheel);
    ZoomToAnchored(m_zoomTarget * factor,
                   ImVec2(io.MousePos.x - origin.x,
                          io.MousePos.y - origin.y));
  }

  // Drag adds mouse delta to pan target.
  if (active &&
      (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f) ||
       ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))) {
    m_panTarget.x += io.MouseDelta.x;
    m_panTarget.y += io.MouseDelta.y;
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
  } else if (hovered) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  }

  // Bounds with margin computed against TARGET zoom so target stays reachable.
  const float kMargin = 80.0f;
  auto clampPan = [&](ImVec2 v, float zoom) {
    const float w = texW * zoom, h = texH * zoom;
    if (w <= avail.x) {
      const float c = (avail.x - w) * 0.5f;
      v.x = std::clamp(v.x, c - kMargin, c + kMargin);
    } else {
      v.x = std::clamp(v.x, avail.x - w - kMargin, kMargin);
    }
    if (h <= avail.y) {
      const float c = (avail.y - h) * 0.5f;
      v.y = std::clamp(v.y, c - kMargin, c + kMargin);
    } else {
      v.y = std::clamp(v.y, avail.y - h - kMargin, kMargin);
    }
    return v;
  };
  m_panTarget = clampPan(m_panTarget, m_zoomTarget);

  // Smooth zoom + pan with identical exp-decay coefficient so the anchor stays
  // consistent throughout the lerp. ~150ms settle.
  const float dt = std::clamp(io.DeltaTime, 1.0f / 240.0f, 1.0f / 30.0f);
  const float k  = 1.0f - std::exp(-18.0f * dt);
  m_zoom  += (m_zoomTarget  - m_zoom)  * k;
  m_pan.x += (m_panTarget.x - m_pan.x) * k;
  m_pan.y += (m_panTarget.y - m_pan.y) * k;

  // Snap when within sub-pixel of target to prevent infinite tiny lerping.
  if (std::abs(m_zoomTarget - m_zoom) < 0.0005f)    m_zoom  = m_zoomTarget;
  if (std::abs(m_panTarget.x - m_pan.x) < 0.25f)    m_pan.x = m_panTarget.x;
  if (std::abs(m_panTarget.y - m_pan.y) < 0.25f)    m_pan.y = m_panTarget.y;

  const float imgW = texW * m_zoom;
  const float imgH = texH * m_zoom;

  // ── Apply alpha toggle (CPU-side recompute + Update -- see the header's
  //    doc comment on why this replaces GL's runtime swizzle) ───────────
  if (alphaChanged) {
    ApplyAlphaToggle();
  }

  // ── Draw the image ───────────────────────────────────────────────────
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 p0(origin.x + m_pan.x, origin.y + m_pan.y);
  const ImVec2 p1(p0.x + imgW, p0.y + imgH);
  dl->PushClipRect(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), true);

  // Draw checkerboard background when showing alpha (so transparency is visible)
  if (m_showAlpha) {
    // No checkerboard needed for alpha-as-grayscale mode; just dark bg
    dl->AddRectFilled(p0, p1, IM_COL32(30, 30, 30, 255));
  }

  dl->AddImage(m_texId, p0, p1);
  dl->PopClipRect();

  if (m_texPool) m_texPool->AdvanceFrame();
}

} // namespace Onyx::Viewers
