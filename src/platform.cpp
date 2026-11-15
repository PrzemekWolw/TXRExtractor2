#include "platform.hpp"

#include <cstdlib>
#include <iterator>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <commdlg.h>
#include <objbase.h>
#include <shlobj.h>
#else
#include <cstdio>
#endif
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

std::string u8str(const fs::path& path)
{
    return path.string();
}

fs::path exeDir()
{
#if defined(_WIN32)
    wchar_t buffer[MAX_PATH * 4];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, DWORD(std::size(buffer)));
    return fs::path(std::wstring(buffer, length)).parent_path();
#elif defined(__APPLE__)
    char buffer[4096];
    uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) != 0) {
        return fs::current_path();
    }
    std::error_code ec;
    return fs::canonical(buffer, ec).parent_path();
#else
    std::error_code ec;
    return fs::read_symlink("/proc/self/exe", ec).parent_path();
#endif
}

fs::path userConfigDir()
{
#if defined(_WIN32)
    const wchar_t* appData = _wgetenv(L"APPDATA");
    fs::path base = appData ? fs::path(appData) : fs::path();
    return base / "TXRExtractor2";
#elif defined(__APPLE__)
    const char* home = std::getenv("HOME");
    fs::path base = home ? fs::path(home) / "Library" / "Application Support" : fs::path();
    return base / "TXRExtractor2";
#else
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
        return fs::path(xdg) / "TXRExtractor2";
    }
    const char* home = std::getenv("HOME");
    return home ? fs::path(home) / ".config" / "TXRExtractor2" : fs::path();
#endif
}

std::string pickPath(bool directory)
{
#if defined(_WIN32)
    wchar_t file[MAX_PATH] = {};
    if (directory) {
        BROWSEINFOW bi{};
        bi.lpszTitle = L"Output folder";
        bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
        if (!pidl) {
            return {};
        }
        SHGetPathFromIDListW(pidl, file);
        CoTaskMemFree(pidl);
        return fs::path(file).string();
    }
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"All files\0*.*\0";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) {
        return {};
    }
    return fs::path(file).string();
#else
    const char* cmd = directory ? "zenity --file-selection --directory 2>/dev/null"
                                : "zenity --file-selection 2>/dev/null";
    FILE* pipe = popen(cmd, "r");
    if (!pipe) {
        return {};
    }
    std::string out;
    char buf[1024];
    while (std::fgets(buf, sizeof(buf), pipe)) {
        out += buf;
    }
    pclose(pipe);
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r')) {
        out.pop_back();
    }
    return out;
#endif
}
