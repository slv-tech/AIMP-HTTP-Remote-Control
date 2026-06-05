#pragma once

#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <commctrl.h>

#ifndef EM_SETCUEBANNER
#define EM_SETCUEBANNER 0x1501
#endif

#include <thread>
#include <mutex>
#include <string>
#include <sstream>
#include <vector>
#include <map>
#include <algorithm>
#include <functional>
#include <set>
#include <cstdint>

#include "json.hpp"
using json = nlohmann::json;

#include "apiPlugin.h"
#include "apiPlayer.h"
#include "apiPlaylists.h"
#include "apiObjects.h"
#include "apiFileManager.h"
#include "apiThreading.h"
#include "apiMessages.h"
#include "apiOptions.h"
