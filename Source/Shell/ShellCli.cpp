#include <cstdio>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "Onyx/Core/Cli/Run.h"
#include "Onyx/Core/Services/Paths.h"
#include "Onyx/Shell/Shell.h"

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <io.h>
#include <shellapi.h>

#pragma comment(lib, "shell32.lib")
#endif

namespace Onyx::Shell {

namespace {

#if defined(_WIN32)

std::string Narrow(std::wstring_view text, UINT codePage) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(codePage, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) return {};
    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(codePage, 0, text.data(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

// The CRT's narrow argv is in the ANSI code page and loses what that page cannot hold, so the
// process's own arguments are read again from the wide command line. An argv that is not the
// process's own (one a host built, already UTF-8) does not match it and is used as given.
bool RebuildFromCommandLine(int argc, char** argv, std::vector<std::string>& storage) {
    int count = 0;
    wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!wide) return false;

    bool ownCommandLine = count == argc;
    for (int i = 0; ownCommandLine && i < count; ++i) {
        ownCommandLine = Narrow(wide[i], CP_ACP) == std::string_view(argv[i]);
    }
    if (ownCommandLine) {
        for (int i = 0; i < count; ++i) storage.push_back(Narrow(wide[i], CP_UTF8));
    }
    LocalFree(wide);
    return ownCommandLine;
}

// Switches a console standard output to UTF-8 for the object's lifetime and restores the code page
// afterwards, since the console keeps it after the process exits. Redirected output is left alone.
class Utf8Console {
public:
    Utf8Console() {
        if (!_isatty(_fileno(stdout))) return;
        m_previous = GetConsoleOutputCP();
        if (m_previous == CP_UTF8 || !SetConsoleOutputCP(CP_UTF8)) m_previous = 0;
    }
    ~Utf8Console() {
        if (m_previous != 0) SetConsoleOutputCP(m_previous);
    }

    Utf8Console(const Utf8Console&) = delete;
    Utf8Console& operator=(const Utf8Console&) = delete;

private:
    UINT m_previous = 0;
};

#endif

// The arguments one call works on, as UTF-8. The storage lives as long as the object.
class Utf8Args {
public:
    Utf8Args(int argc, char** argv) {
#if defined(_WIN32)
        if (RebuildFromCommandLine(argc, argv, m_storage)) {
            for (std::string& arg : m_storage) m_pointers.push_back(arg.data());
            m_pointers.push_back(nullptr);
            return;
        }
#endif
        m_pointers.assign(argv, argv + argc);
        m_pointers.push_back(nullptr);
    }

    int argc() const { return static_cast<int>(m_pointers.size()) - 1; }
    char** argv() { return m_pointers.data(); }

private:
    std::vector<std::string> m_storage;
    std::vector<char*> m_pointers;
};

}

int RunCli(Shell& shell, int argc, char** argv, std::ostream& out, std::ostream& err) {
    Utf8Args args(argc, argv);
    if (args.argc() == 2 && (std::string_view(args.argv()[1]) == "--help" || std::string_view(args.argv()[1]) == "-h")) {
        Cli::Run(shell.Workspace(), shell.Verbs(), 1, args.argv(), out, out);
        return Cli::kOk;
    }
    const int code = Cli::Run(shell.Workspace(), shell.Verbs(), args.argc(), args.argv(), out, err);
    if (code == Cli::kUsage) {
        err << "shell.args: the command line names no known verb or misuses one\n";
    }
    return code;
}

int RunCli(Shell& shell, int argc, char** argv) {
#if defined(_WIN32)
    const Utf8Console console;
#endif
    return RunCli(shell, argc, argv, std::cout, std::cerr);
}

ArgsRoute RouteArgs(int argc, char** argv, const Cli::VerbTable& verbs) {
    Utf8Args args(argc, argv);
    if (args.argc() <= 1) {
        return {ArgsRoute::Kind::Gui, {}};
    }
    if (args.argc() == 2 && !verbs.Find(args.argv()[1])) {
        std::filesystem::path file = Services::Paths::FromUtf8(args.argv()[1]);
        std::error_code ec;
        if (std::filesystem::is_regular_file(file, ec)) {
            return {ArgsRoute::Kind::GuiOpen, std::move(file)};
        }
    }
    return {ArgsRoute::Kind::Cli, {}};
}

}
