#include "Onyx/Shell/NativeWindow.h"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <windowsx.h>
#include <dwmapi.h>

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#pragma comment(lib, "dwmapi.lib")

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

namespace Onyx::Shell {

namespace {

constexpr wchar_t kWindowProp[] = L"OnyxShellNativeWindow";
constexpr wchar_t kPreviousProp[] = L"OnyxShellPreviousProc";

LRESULT HitTest(HWND hwnd, LPARAM lParam, const TitleBarRect& caption, GLFWwindow* window) {
    if (glfwGetWindowMonitor(window) != nullptr) {
        return HTCLIENT;
    }
    const POINT cursor{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
    RECT bounds;
    if (!GetWindowRect(hwnd, &bounds)) {
        return HTNOWHERE;
    }
    const POINT border{GetSystemMetrics(SM_CXFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER),
                       GetSystemMetrics(SM_CYFRAME) + GetSystemMetrics(SM_CXPADDEDBORDER)};

    constexpr int kClient = 0;
    constexpr int kLeft = 0b0001;
    constexpr int kRight = 0b0010;
    constexpr int kTop = 0b0100;
    constexpr int kBottom = 0b1000;
    const int region = kLeft * (cursor.x < bounds.left + border.x) | kRight * (cursor.x >= bounds.right - border.x) |
                       kTop * (cursor.y < bounds.top + border.y) | kBottom * (cursor.y >= bounds.bottom - border.y);

    if (region != kClient && caption.blocked) {
        return HTCLIENT;
    }
    switch (region) {
    case kLeft: return HTLEFT;
    case kRight: return HTRIGHT;
    case kTop: return HTTOP;
    case kBottom: return HTBOTTOM;
    case kTop | kLeft: return HTTOPLEFT;
    case kTop | kRight: return HTTOPRIGHT;
    case kBottom | kLeft: return HTBOTTOMLEFT;
    case kBottom | kRight: return HTBOTTOMRIGHT;
    default: break;
    }
    if (caption.blocked) {
        return HTCLIENT;
    }
    const float x = static_cast<float>(cursor.x - bounds.left);
    const float y = static_cast<float>(cursor.y - bounds.top);
    if (x >= caption.x && x < caption.x + caption.width && y >= caption.y && y < caption.y + caption.height) {
        return HTCAPTION;
    }
    return HTCLIENT;
}

LRESULT CALLBACK BorderlessProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = static_cast<NativeWindow*>(GetPropW(hwnd, kWindowProp));
    const auto previous = reinterpret_cast<WNDPROC>(GetPropW(hwnd, kPreviousProp));
    if (self == nullptr || previous == nullptr) {
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
    switch (message) {
    case WM_NCACTIVATE:
    case WM_NCPAINT:
        return DefWindowProcW(hwnd, message, wParam, lParam);
    case WM_NCCALCSIZE:
        if (wParam == TRUE && lParam != 0) {
            auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
            RECT& rect = params->rgrc[0];
            const RECT client = rect;
            CallWindowProcW(previous, hwnd, message, wParam, lParam);
            if (IsZoomed(hwnd)) {
                WINDOWINFO info{sizeof(info)};
                GetWindowInfo(hwnd, &info);
                const LONG inset = static_cast<LONG>(info.cyWindowBorders);
                rect = {client.left + inset, client.top + inset, client.right - inset, client.bottom - inset + 1};
            } else {
                rect = client;
            }
            return WVR_REDRAW;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    case WM_WINDOWPOSCHANGING:
        reinterpret_cast<LPWINDOWPOS>(lParam)->flags |= SWP_NOCOPYBITS;
        break;
    case WM_NCHITTEST:
        return HitTest(hwnd, lParam, self->GetTitleBarRect(), self->Handle());
    default:
        break;
    }
    return CallWindowProcW(previous, hwnd, message, wParam, lParam);
}

}

void NativeWindow::InstallPlatform() {
    if (!m_options.borderless) {
        return;
    }
    HWND hwnd = glfwGetWin32Window(m_window);
    // GLFW gives an undecorated window WS_POPUP, which has no native resize or snap. WS_OVERLAPPEDWINDOW
    // restores both once the frame is drawn by the WM_NCCALCSIZE handler, and WS_EX_COMPOSITED removes
    // the flicker while resizing. Both are set here, once.
    LONG style = GetWindowLongW(hwnd, GWL_STYLE);
    style |= WS_OVERLAPPEDWINDOW;
    style &= ~WS_POPUP;
    SetWindowLongW(hwnd, GWL_STYLE, style);
    SetWindowLongW(hwnd, GWL_EXSTYLE, GetWindowLongW(hwnd, GWL_EXSTYLE) | WS_EX_COMPOSITED);

    m_previousProc = GetWindowLongPtrW(hwnd, GWLP_WNDPROC);
    SetPropW(hwnd, kWindowProp, this);
    SetPropW(hwnd, kPreviousProp, reinterpret_cast<HANDLE>(m_previousProc));
    SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&BorderlessProc));

    const MARGINS margins{1, 1, 1, 1};
    DwmExtendFrameIntoClientArea(hwnd, &margins);
    const DWORD policy = DWMNCRP_ENABLED;
    DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy));
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED | SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE);
}

void NativeWindow::RemovePlatform() {
    if (m_previousProc == 0) {
        return;
    }
    HWND hwnd = glfwGetWin32Window(m_window);
    SetWindowLongPtrW(hwnd, GWLP_WNDPROC, m_previousProc);
    RemovePropW(hwnd, kWindowProp);
    RemovePropW(hwnd, kPreviousProp);
    m_previousProc = 0;
}

}

#endif
