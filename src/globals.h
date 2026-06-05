#pragma once
#include "pch.h"

// DLL instance (для Win32 UI)
extern HINSTANCE  g_hInstance;

// AIMP core
extern IAIMPCore* g_core;

// HTTP сервер
extern std::mutex g_mutex;
extern int        g_port;
// Bind mode: 0 = 127.0.0.1, 1 = 0.0.0.0, 2 = конкретный IP (g_bindIp)
extern int        g_bindMode;
extern std::wstring g_bindIp;
extern bool       g_running;
extern std::thread g_serverThread;
extern SOCKET     g_serverSocket;

// Политика доступа
extern bool        g_allowListEnabled;
extern std::string g_allowList;

// Фокус навигации
extern std::mutex g_focusMutex;
extern int g_focusPlaylistIdx;
extern int g_focusTrackIdx;
