#include "Onyx/Ui/Viewers/ImageView.h"

#include <Onyx/Ui/TexturePool.h>
#include <Onyx/Ui/UiContext.h>
#include <Onyx/Ui/Widgets.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace Onyx::Ui {

namespace {

constexpr float kMinZoom = 0.125f;
constexpr float kMaxZoom = 16.0f;
constexpr float kPanMargin = 80.0f;

const char* FormatName(Domain::PixelFormat format) {
    switch (format) {
    case Domain::PixelFormat::Rgba8: return "RGBA8";
    case Domain::PixelFormat::Bc1: return "BC1";
    case Domain::PixelFormat::Bc3: return "BC3";
    case Domain::PixelFormat::Bc4: return "BC4";
    case Domain::PixelFormat::Bc5: return "BC5";
    case Domain::PixelFormat::Bc7: return "BC7";
    case Domain::PixelFormat::RgbaF16: return "RGBA16F";
    }
    return "?";
}

Domain::Texture AlphaPlane(const Domain::Texture& source) {
    Domain::Texture plane;
    plane.name = source.name + ".alpha";
    plane.width = source.width;
    plane.height = source.height;
    plane.format = Domain::PixelFormat::Rgba8;
    const size_t count = static_cast<size_t>(source.width) * source.height;
    plane.pixels.resize(count * 4);
    for (size_t i = 0; i < count; ++i) {
        const std::byte a = source.pixels[i * 4 + 3];
        plane.pixels[i * 4 + 0] = a;
        plane.pixels[i * 4 + 1] = a;
        plane.pixels[i * 4 + 2] = a;
        plane.pixels[i * 4 + 3] = std::byte{255};
    }
    return plane;
}

}

ImageView::ImageView(Modules::DocumentId doc, Modules::NodePath node, std::string name, Domain::Texture texture)
    : m_doc(doc),
      m_node(std::move(node)),
      m_name(std::move(name)),
      m_id("onyx.image." + std::to_string(doc) + "." + m_name),
      m_texture(std::move(texture)) {}

ImageView::~ImageView() {
    if (!m_pool) return;
    if (m_color != ImTextureID_Invalid) m_pool->Remove(m_color);
    if (m_alpha != ImTextureID_Invalid) m_pool->Remove(m_alpha);
}

void ImageView::Upload(TexturePool& pool) {
    auto id = pool.Create(m_texture);
    if (!id.ok()) {
        m_fault = id.error();
        return;
    }
    m_pool = &pool;
    m_color = id.value();
}

void ImageView::UploadAlpha(TexturePool& pool) {
    auto id = pool.Create(AlphaPlane(m_texture));
    if (!id.ok()) {
        m_fault = id.error();
        m_showAlpha = false;
        return;
    }
    m_alpha = id.value();
}

void ImageView::ZoomTo(float zoom, ImVec2 anchor) {
    zoom = std::clamp(zoom, kMinZoom, kMaxZoom);
    if (zoom == m_zoomTarget) return;
    const ImVec2 local(anchor.x - m_panTarget.x, anchor.y - m_panTarget.y);
    const float scale = zoom / m_zoomTarget;
    m_panTarget = ImVec2(anchor.x - local.x * scale, anchor.y - local.y * scale);
    m_zoomTarget = zoom;
}

void ImageView::Draw(UiContext& ui) {
    if (m_color == ImTextureID_Invalid && !m_fault && ui.textures) Upload(*ui.textures);

    const bool rgba8 = m_texture.format == Domain::PixelFormat::Rgba8;
    if (m_showAlpha && rgba8 && m_alpha == ImTextureID_Invalid && m_pool && m_color != ImTextureID_Invalid) {
        UploadAlpha(*m_pool);
    }

    ImGui::Text("%ux%u  %s", m_texture.width, m_texture.height, FormatName(m_texture.format));
    ImGui::SameLine();
    const bool zoomIn = Widgets::ToolbarButton("+");
    ImGui::SameLine();
    const bool zoomOut = Widgets::ToolbarButton("-");
    ImGui::SameLine();
    const bool oneToOne = Widgets::ToolbarButton("1:1");
    ImGui::SameLine();
    const bool fit = Widgets::ToolbarButton("Fit");
    ImGui::SameLine();
    ImGui::BeginDisabled(!rgba8);
    ImGui::Checkbox("Alpha", &m_showAlpha);
    ImGui::EndDisabled();
    ImGui::Separator();

    if (m_fault) {
        Widgets::StatusText(Services::Severity::Error, (m_fault->code + ": " + m_fault->message).c_str());
        return;
    }
    if (m_color == ImTextureID_Invalid) {
        Widgets::Hint("No texture pool to show the image with");
        return;
    }

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x <= 1.0f || avail.y <= 1.0f) return;
    const float texW = static_cast<float>(m_texture.width);
    const float texH = static_cast<float>(m_texture.height);
    const ImVec2 origin = ImGui::GetCursorScreenPos();

    const auto fitZoom = [&] { return std::min(avail.x / texW, avail.y / texH); };
    const auto centered = [&](float zoom) {
        return ImVec2((avail.x - texW * zoom) * 0.5f, (avail.y - texH * zoom) * 0.5f);
    };
    const ImVec2 middle(avail.x * 0.5f, avail.y * 0.5f);

    if (!m_fitted) {
        m_zoomTarget = m_zoom = std::clamp(fitZoom(), kMinZoom, kMaxZoom);
        m_panTarget = m_pan = centered(m_zoom);
        m_fitted = true;
    }
    if (zoomIn) ZoomTo(m_zoomTarget * 1.5f, middle);
    if (zoomOut) ZoomTo(m_zoomTarget / 1.5f, middle);
    if (oneToOne) ZoomTo(1.0f, middle);
    if (fit) {
        m_zoomTarget = std::clamp(fitZoom(), kMinZoom, kMaxZoom);
        m_panTarget = centered(m_zoomTarget);
    }

    ImGui::InvisibleButton("##canvas", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const bool active = ImGui::IsItemActive();
    const ImGuiIO& io = ImGui::GetIO();

    if (hovered && io.MouseWheel != 0.0f) {
        ZoomTo(m_zoomTarget * std::pow(1.15f, io.MouseWheel), ImVec2(io.MousePos.x - origin.x, io.MousePos.y - origin.y));
    }
    if (active && (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f))) {
        m_panTarget.x += io.MouseDelta.x;
        m_panTarget.y += io.MouseDelta.y;
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    }

    const auto clampPan = [&](ImVec2 v, float zoom) {
        const float w = texW * zoom;
        const float h = texH * zoom;
        if (w <= avail.x) {
            const float c = (avail.x - w) * 0.5f;
            v.x = std::clamp(v.x, c - kPanMargin, c + kPanMargin);
        } else {
            v.x = std::clamp(v.x, avail.x - w - kPanMargin, kPanMargin);
        }
        if (h <= avail.y) {
            const float c = (avail.y - h) * 0.5f;
            v.y = std::clamp(v.y, c - kPanMargin, c + kPanMargin);
        } else {
            v.y = std::clamp(v.y, avail.y - h - kPanMargin, kPanMargin);
        }
        return v;
    };
    m_panTarget = clampPan(m_panTarget, m_zoomTarget);

    const float dt = std::clamp(io.DeltaTime, 1.0f / 240.0f, 1.0f / 30.0f);
    const float k = 1.0f - std::exp(-18.0f * dt);
    m_zoom += (m_zoomTarget - m_zoom) * k;
    m_pan.x += (m_panTarget.x - m_pan.x) * k;
    m_pan.y += (m_panTarget.y - m_pan.y) * k;
    if (std::abs(m_zoomTarget - m_zoom) < 0.0005f) m_zoom = m_zoomTarget;
    if (std::abs(m_panTarget.x - m_pan.x) < 0.25f) m_pan.x = m_panTarget.x;
    if (std::abs(m_panTarget.y - m_pan.y) < 0.25f) m_pan.y = m_panTarget.y;

    const ImVec2 p0(origin.x + m_pan.x, origin.y + m_pan.y);
    const ImVec2 p1(p0.x + texW * m_zoom, p0.y + texH * m_zoom);
    const ImTextureID shown = m_showAlpha && m_alpha != ImTextureID_Invalid ? m_alpha : m_color;

    ImDrawList* list = ImGui::GetWindowDrawList();
    list->PushClipRect(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), true);
    list->AddImage(shown, p0, p1);

    if (hovered && rgba8 && io.MousePos.x >= p0.x && io.MousePos.x < p1.x && io.MousePos.y >= p0.y && io.MousePos.y < p1.y) {
        const uint32_t x = std::min(static_cast<uint32_t>((io.MousePos.x - p0.x) / m_zoom), m_texture.width - 1);
        const uint32_t y = std::min(static_cast<uint32_t>((io.MousePos.y - p0.y) / m_zoom), m_texture.height - 1);
        const size_t at = (static_cast<size_t>(y) * m_texture.width + x) * 4;
        char text[96];
        std::snprintf(text, sizeof text, "(%u, %u)  R %u  G %u  B %u  A %u", x, y,
                      std::to_integer<unsigned>(m_texture.pixels[at]), std::to_integer<unsigned>(m_texture.pixels[at + 1]),
                      std::to_integer<unsigned>(m_texture.pixels[at + 2]), std::to_integer<unsigned>(m_texture.pixels[at + 3]));
        list->AddText(ImVec2(origin.x + 6.0f, origin.y + avail.y - ImGui::GetTextLineHeight() - 4.0f),
                      ImGui::GetColorU32(ImGuiCol_Text), text);
    }
    list->PopClipRect();
}

}
