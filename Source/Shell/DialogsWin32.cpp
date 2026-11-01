#include "Onyx/Shell/Dialogs.h"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>

#include "Onyx/Core/Services/Log.h"

#pragma comment(lib, "ole32.lib")

namespace Onyx::Shell {

namespace {

std::wstring ToWide(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), length);
    return wide;
}

template <class T>
class ComPtr {
public:
    ComPtr() = default;
    ComPtr(const ComPtr&) = delete;
    ComPtr& operator=(const ComPtr&) = delete;
    ~ComPtr() {
        if (m_ptr) {
            m_ptr->Release();
        }
    }
    T** Put() { return &m_ptr; }
    T* operator->() const { return m_ptr; }
    T* Get() const { return m_ptr; }

private:
    T* m_ptr = nullptr;
};

struct ComScope {
    ComScope() : m_hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE)) {}
    ~ComScope() {
        if (SUCCEEDED(m_hr)) {
            CoUninitialize();
        }
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;

    HRESULT m_hr;
};

void LogFailure(const char* what, HRESULT hr) {
    Services::Log::Default().Write(Services::LogLevel::Warn, "shell.dialog", "{} failed: 0x{:08X}", what,
                                   static_cast<unsigned>(hr));
}

class Win32Dialogs final : public IDialogs {
public:
    std::optional<std::filesystem::path> OpenFile(std::span<const Modules::OpenFilter> filters) override {
        return Pick(false, false, filters, {});
    }
    std::optional<std::filesystem::path> OpenFolder() override { return Pick(true, false, {}, {}); }
    std::optional<std::filesystem::path> SaveFile(std::string_view defaultName) override {
        return Pick(false, true, {}, defaultName);
    }

private:
    static std::optional<std::filesystem::path> Pick(bool folder, bool save,
                                                     std::span<const Modules::OpenFilter> filters,
                                                     std::string_view defaultName) {
        ComScope com;
        ComPtr<IFileDialog> dialog;
        HRESULT hr = save ? CoCreateInstance(__uuidof(FileSaveDialog), nullptr, CLSCTX_INPROC_SERVER,
                                             IID_PPV_ARGS(dialog.Put()))
                          : CoCreateInstance(__uuidof(FileOpenDialog), nullptr, CLSCTX_INPROC_SERVER,
                                             IID_PPV_ARGS(dialog.Put()));
        if (FAILED(hr)) {
            LogFailure("creating the dialog", hr);
            return std::nullopt;
        }
        DWORD options = 0;
        if (SUCCEEDED(dialog->GetOptions(&options))) {
            options |= FOS_FORCEFILESYSTEM | (folder ? FOS_PICKFOLDERS : 0);
            dialog->SetOptions(options);
        }
        if (!folder) {
            std::vector<std::wstring> names;
            std::vector<std::wstring> patterns;
            for (const Modules::OpenFilter& filter : filters) {
                std::string joined;
                for (const std::string& glob : FilterGlobs(filter)) {
                    joined += (joined.empty() ? "" : ";") + glob;
                }
                if (joined.empty()) {
                    continue;
                }
                names.push_back(ToWide(filter.label));
                patterns.push_back(ToWide(joined));
            }
            names.emplace_back(L"All files");
            patterns.emplace_back(L"*.*");
            std::vector<COMDLG_FILTERSPEC> specs;
            for (size_t i = 0; i < names.size(); ++i) {
                specs.push_back({names[i].c_str(), patterns[i].c_str()});
            }
            dialog->SetFileTypes(static_cast<UINT>(specs.size()), specs.data());
        }
        if (!defaultName.empty()) {
            dialog->SetFileName(ToWide(defaultName).c_str());
        }
        hr = dialog->Show(GetActiveWindow());
        if (hr == HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
            return std::nullopt;
        }
        if (FAILED(hr)) {
            LogFailure("showing the dialog", hr);
            return std::nullopt;
        }
        ComPtr<IShellItem> item;
        hr = dialog->GetResult(item.Put());
        if (FAILED(hr)) {
            LogFailure("reading the dialog result", hr);
            return std::nullopt;
        }
        PWSTR name = nullptr;
        hr = item->GetDisplayName(SIGDN_FILESYSPATH, &name);
        if (FAILED(hr)) {
            LogFailure("resolving the chosen path", hr);
            return std::nullopt;
        }
        std::filesystem::path chosen(name);
        CoTaskMemFree(name);
        return chosen;
    }
};

}

std::unique_ptr<IDialogs> NativeDialogs() {
    return std::make_unique<Win32Dialogs>();
}

}

#endif
