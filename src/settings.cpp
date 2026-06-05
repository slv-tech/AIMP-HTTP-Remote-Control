#include "settings.h"
#include "globals.h"
#include "utils.h"

std::wstring GetSettingsPath() {
    wchar_t appData[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"APPDATA", appData, MAX_PATH)) {
        std::wstring dir = std::wstring(appData) + L"\\AIMP";
        CreateDirectoryW(dir.c_str(), nullptr);
        return dir + L"\\AimpHttpControl.ini";
    }
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(g_hInstance, path, MAX_PATH);
    std::wstring s(path);
    auto pos = s.rfind(L'\\');
    if (pos != std::wstring::npos) s = s.substr(0, pos + 1);
    return s + L"AimpHttpControl.ini";
}

void LoadSettings() {
    std::wstring ini = GetSettingsPath();
    g_port     = GetPrivateProfileIntW(L"Server", L"Port",     19122, ini.c_str());
    g_bindMode = GetPrivateProfileIntW(L"Server", L"BindMode", 1,     ini.c_str());
    if (g_port < 1 || g_port > 65535) g_port = 19122;
    if (g_bindMode < 0 || g_bindMode > 2) g_bindMode = 1;

    wchar_t bindIpBuf[64] = {};
    GetPrivateProfileStringW(L"Server", L"BindIP", L"", bindIpBuf, 64, ini.c_str());
    g_bindIp = std::wstring(bindIpBuf);

    g_allowListEnabled = GetPrivateProfileIntW(L"Access", L"AllowListEnabled", 0, ini.c_str()) != 0;
    wchar_t alBuf[1024] = {};
    GetPrivateProfileStringW(L"Access", L"AllowList", L"", alBuf, 1024, ini.c_str());
    g_allowList = WStrToUtf8(std::wstring(alBuf));
}

void SaveSettings() {
    std::wstring ini = GetSettingsPath();
    wchar_t buf[16];
    wsprintfW(buf, L"%d", g_port);
    WritePrivateProfileStringW(L"Server", L"Port", buf, ini.c_str());
    wsprintfW(buf, L"%d", g_bindMode);
    WritePrivateProfileStringW(L"Server", L"BindMode", buf, ini.c_str());
    WritePrivateProfileStringW(L"Server", L"BindIP",
        g_bindIp.empty() ? L"" : g_bindIp.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"Access", L"AllowListEnabled",
        g_allowListEnabled ? L"1" : L"0", ini.c_str());
    std::wstring alW = Utf8ToWStr(g_allowList);
    WritePrivateProfileStringW(L"Access", L"AllowList",
        alW.empty() ? L"" : alW.c_str(), ini.c_str());
}
