#include "globals.h"

HINSTANCE  g_hInstance = nullptr;
IAIMPCore* g_core      = nullptr;
std::mutex g_mutex;
int        g_port      = 19122;
int        g_bindMode  = 1;
std::wstring g_bindIp  = L"";
bool       g_running   = false;
std::thread g_serverThread;
SOCKET     g_serverSocket = INVALID_SOCKET;

bool        g_allowListEnabled = false;
std::string g_allowList        = "";

std::mutex g_focusMutex;
int g_focusPlaylistIdx = 0;
int g_focusTrackIdx    = 0;
