// aimp_http_api.cpp
#define _WIN32_WINNT 0x0A00
#define WINVER 0x0A00

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <thread>
#include <mutex>
#include <string>
#include <ctime>
#include <sstream>
#include <vector>
#include <map>
#include <algorithm>

#include "third_party/json.hpp"
using json = nlohmann::json;

#include "sdk/apiPlugin.h"
#include "sdk/apiPlayer.h"
#include "sdk/apiPlaylists.h"
#include "sdk/apiObjects.h"

// ==========================================
// Глобальные переменные
// ==========================================
IAIMPCore* g_core = nullptr;
std::mutex g_mutex;
int g_port = 3553;
bool g_running = false;
std::thread g_serverThread;

// ==========================================
// Утилиты
// ==========================================
std::string WStr(const wchar_t* w) {
    if (!w) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, 0, 0, 0, 0);
    if (len <= 0) return "";
    std::string s(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, w, -1, &s[0], len, 0, 0);
    return s;
}

std::string GenerateId(const std::string& prefix, int index) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%s_%03d", prefix.c_str(), index);
    return buf;
}

// ==========================================
// Вспомогательные функции AIMP API
// ==========================================
IAIMPPlaylistProperties* GetPlaylistProps(IAIMPPlaylist* pl) {
    if (!pl) return nullptr;
    IAIMPPlaylistProperties* props = nullptr;
    pl->QueryInterface(IID_IAIMPPlaylistProperties, (void**)&props);
    return props;
}

std::string GetPlaylistName(IAIMPPlaylist* pl) {
    std::string result = "Unknown";
    IAIMPPlaylistProperties* props = GetPlaylistProps(pl);
    if (props) {
        IAIMPString* name = nullptr;
        if (props->GetValueAsObject(AIMP_PLAYLIST_PROPID_NAME, IID_IAIMPString, (void**)&name) == S_OK && name) {
            result = WStr(name->GetData());
            name->Release();
        }
        props->Release();
    }
    return result;
}

int GetPlayingIndex(IAIMPPlaylist* pl) {
    int index = -1;
    IAIMPPlaylistProperties* props = GetPlaylistProps(pl);
    if (props) {
        props->GetValueAsInt32(AIMP_PLAYLIST_PROPID_PLAYINGINDEX, &index);
        props->Release();
    }
    return index;
}

int GetFocusedIndex(IAIMPPlaylist* pl) {
    int index = -1;
    IAIMPPlaylistProperties* props = GetPlaylistProps(pl);
    if (props) {
        props->GetValueAsInt32(AIMP_PLAYLIST_PROPID_FOCUSINDEX, &index);
        props->Release();
    }
    return index;
}

double GetPlaylistDuration(IAIMPPlaylist* pl) {
    double duration = 0;
    IAIMPPlaylistProperties* props = GetPlaylistProps(pl);
    if (props) {
        props->GetValueAsFloat(AIMP_PLAYLIST_PROPID_DURATION, &duration);
        props->Release();
    }
    return duration;
}

// Получить ID плейлиста
std::string GetPlaylistId(IAIMPPlaylist* pl) {
    std::string result = "";
    IAIMPPlaylistProperties* props = GetPlaylistProps(pl);
    if (props) {
        IAIMPString* idStr = nullptr;
        if (props->GetValueAsObject(AIMP_PLAYLIST_PROPID_ID, IID_IAIMPString, (void**)&idStr) == S_OK && idStr) {
            result = WStr(idStr->GetData());
            idStr->Release();
        }
        props->Release();
    }
    return result;
}

// Получить файловую информацию трека
void GetFileInfo(IAIMPPlaylistItem* item, json& result) {
    IAIMPString* s = nullptr;
    
    if (item->GetValueAsObject(AIMP_PLAYLISTITEM_PROPID_FILENAME, IID_IAIMPString, (void**)&s) == S_OK && s) {
        result["file_path"] = WStr(s->GetData());
        s->Release();
    }
    
    IUnknown* unk = nullptr;
    if (item->GetValueAsObject(AIMP_PLAYLISTITEM_PROPID_FILEINFO, IID_IAIMPPropertyList, (void**)&unk) == S_OK && unk) {
        IAIMPPropertyList* props = static_cast<IAIMPPropertyList*>(unk);
        
        if (props->GetValueAsObject(1, IID_IAIMPString, (void**)&s) == S_OK && s) {
            result["title"] = WStr(s->GetData()); s->Release();
        }
        if (props->GetValueAsObject(2, IID_IAIMPString, (void**)&s) == S_OK && s) {
            result["artist"] = WStr(s->GetData()); s->Release();
        }
        if (props->GetValueAsObject(3, IID_IAIMPString, (void**)&s) == S_OK && s) {
            result["album"] = WStr(s->GetData()); s->Release();
        }
        if (props->GetValueAsObject(4, IID_IAIMPString, (void**)&s) == S_OK && s) {
            result["year"] = std::atoi(WStr(s->GetData()).c_str()); s->Release();
        }
        if (props->GetValueAsObject(5, IID_IAIMPString, (void**)&s) == S_OK && s) {
            result["genre"] = WStr(s->GetData()); s->Release();
        }
        
        double duration = 0;
        if (props->GetValueAsFloat(8, &duration) == S_OK) { // 8 = Duration
            result["duration"] = duration;
        }
        
        int bitrate = 0;
        if (props->GetValueAsInt32(9, &bitrate) == S_OK) { // 9 = Bitrate
            result["bitrate"] = bitrate;
        }
        
        props->Release();
    }
    
    // Display text fallback
    if (!result.contains("title")) {
        if (item->GetValueAsObject(AIMP_PLAYLISTITEM_PROPID_DISPLAYTEXT, IID_IAIMPString, (void**)&s) == S_OK && s) {
            result["title"] = WStr(s->GetData());
            s->Release();
        }
    }
}

// ==========================================
// API-функции
// ==========================================
json GetPlayerStatus() {
    json r;
    r["state"] = "stopped";
    r["volume"] = 0;
    r["muted"] = false;
    r["position"] = 0.0;
    r["duration"] = 0.0;
    r["shuffle"] = false;
    r["repeat"] = "none";
    
    if (!g_core) return r;
    
    IAIMPServicePlayer* player = nullptr;
    if (g_core->QueryInterface(IID_IAIMPServicePlayer, (void**)&player) == S_OK && player) {
        double pos = 0;
        player->GetPosition(&pos);
        r["position"] = pos;
        
        float vol = 0;
        player->GetVolume(&vol);
        r["volume"] = (int)(vol * 100);
        
        int st = player->GetState();
        r["state"] = (st == 1) ? "playing" : (st == 2) ? "paused" : "stopped";
        
        player->Release();
    }
    
    // Получаем инфо о текущем треке
    IAIMPServicePlaylistManager* mgr = nullptr;
    if (g_core->QueryInterface(IID_IAIMPServicePlaylistManager, (void**)&mgr) == S_OK && mgr) {
        IAIMPPlaylist* activePl = nullptr;
        if (mgr->GetActivePlaylist(&activePl) == S_OK && activePl) {
            int playingIdx = GetPlayingIndex(activePl);
            if (playingIdx >= 0) {
                IAIMPPlaylistItem* item = nullptr;
                if (activePl->GetItem(playingIdx, IID_IAIMPPlaylistItem, (void**)&item) == S_OK && item) {
                    json trackInfo;
                    GetFileInfo(item, trackInfo);
                    if (trackInfo.contains("duration")) r["duration"] = trackInfo["duration"];
                    item->Release();
                }
            }
            
            // Выбранный трек
            int focusedIdx = GetFocusedIndex(activePl);
            if (focusedIdx >= 0) {
                IAIMPPlaylistItem* item = nullptr;
                if (activePl->GetItem(focusedIdx, IID_IAIMPPlaylistItem, (void**)&item) == S_OK && item) {
                    IAIMPString* s = nullptr;
                    if (item->GetValueAsObject(AIMP_PLAYLISTITEM_PROPID_FILENAME, IID_IAIMPString, (void**)&s) == S_OK && s) {
                        r["filenameselected"] = WStr(s->GetData());
                        s->Release();
                    }
                    item->Release();
                }
            }
            
            // Играющий трек
            if (playingIdx >= 0) {
                IAIMPPlaylistItem* item = nullptr;
                if (activePl->GetItem(playingIdx, IID_IAIMPPlaylistItem, (void**)&item) == S_OK && item) {
                    IAIMPString* s = nullptr;
                    if (item->GetValueAsObject(AIMP_PLAYLISTITEM_PROPID_FILENAME, IID_IAIMPString, (void**)&s) == S_OK && s) {
                        r["filenameplaying"] = WStr(s->GetData());
                        s->Release();
                    }
                    item->Release();
                }
            }
            
            activePl->Release();
        }
        mgr->Release();
    }
    
    return r;
}

json GetPlaylistsResponse() {
    json r;
    r["playlists"] = json::array();
    
    if (!g_core) return r;
    
    IAIMPServicePlaylistManager* mgr = nullptr;
    if (g_core->QueryInterface(IID_IAIMPServicePlaylistManager, (void**)&mgr) != S_OK || !mgr) return r;
    
    IAIMPPlaylist* activePl = nullptr;
    mgr->GetActivePlaylist(&activePl);
    
    int count = mgr->GetLoadedPlaylistCount();
    
    for (int i = 0; i < count; i++) {
        IAIMPPlaylist* pl = nullptr;
        if (mgr->GetLoadedPlaylist(i, &pl) == S_OK && pl) {
            json p;
            p["id"] = GenerateId("pl", i);
            p["name"] = GetPlaylistName(pl);
            p["track_count"] = pl->GetItemCount();
            p["duration"] = GetPlaylistDuration(pl);
            
            // Состояние
            if (activePl == pl) {
                int playingIdx = GetPlayingIndex(pl);
                p["state"] = (playingIdx >= 0) ? "playing" : "selected";
            } else {
                p["state"] = nullptr;
            }
            
            r["playlists"].push_back(p);
            pl->Release();
        }
    }
    
    if (activePl) activePl->Release();
    mgr->Release();
    
    return r;
}

json GetPlaylistResponse(int playlistId) {
    json r;
    
    if (!g_core) { r["error"] = "No core"; return r; }
    
    IAIMPServicePlaylistManager* mgr = nullptr;
    if (g_core->QueryInterface(IID_IAIMPServicePlaylistManager, (void**)&mgr) != S_OK || !mgr) {
        r["error"] = "No playlist manager";
        return r;
    }
    
    IAIMPPlaylist* pl = nullptr;
    if (mgr->GetLoadedPlaylist(playlistId, &pl) != S_OK || !pl) {
        mgr->Release();
        r["error"]["code"] = "PLAYLIST_NOT_FOUND";
        r["error"]["message"] = "Playlist not found";
        return r;
    }
    
    IAIMPPlaylist* activePl = nullptr;
    mgr->GetActivePlaylist(&activePl);
    
    r["id"] = GenerateId("pl", playlistId);
    r["name"] = GetPlaylistName(pl);
    r["track_count"] = pl->GetItemCount();
    r["duration"] = GetPlaylistDuration(pl);
    
    if (activePl == pl) {
        int playingIdx = GetPlayingIndex(pl);
        r["state"] = (playingIdx >= 0) ? "playing" : "selected";
    } else {
        r["state"] = nullptr;
    }
    
    if (activePl) activePl->Release();
    pl->Release();
    mgr->Release();
    
    return r;
}

json GetTracksResponse(int playlistId, int limit, int offset) {
    json r;
    r["playlist_id"] = GenerateId("pl", playlistId);
    r["tracks"] = json::array();
    r["offset"] = offset;
    r["limit"] = limit;
    
    if (!g_core) { r["error"] = "No core"; return r; }
    
    IAIMPServicePlaylistManager* mgr = nullptr;
    if (g_core->QueryInterface(IID_IAIMPServicePlaylistManager, (void**)&mgr) != S_OK || !mgr) {
        r["error"] = "No playlist manager";
        return r;
    }
    
    IAIMPPlaylist* pl = nullptr;
    if (mgr->GetLoadedPlaylist(playlistId, &pl) != S_OK || !pl) {
        mgr->Release();
        r["error"]["code"] = "PLAYLIST_NOT_FOUND";
        r["error"]["message"] = "Playlist not found";
        return r;
    }
    
    int total = pl->GetItemCount();
    r["total"] = total;
    
    int playingIdx = GetPlayingIndex(pl);
    int focusedIdx = GetFocusedIndex(pl);
    
    int end = std::min(offset + limit, total);
    
    for (int i = offset; i < end; i++) {
        IAIMPPlaylistItem* item = nullptr;
        if (pl->GetItem(i, IID_IAIMPPlaylistItem, (void**)&item) == S_OK && item) {
            json t;
            t["id"] = GenerateId("tr", i);
            GetFileInfo(item, t);
            t["position_in_playlist"] = i + 1;
            
            if (i == playingIdx) t["state"] = "playing";
            else if (i == focusedIdx) t["state"] = "selected";
            else t["state"] = nullptr;
            
            r["tracks"].push_back(t);
            item->Release();
        }
    }
    
    pl->Release();
    mgr->Release();
    
    return r;
}

json GetTrackResponse(int playlistId, int trackId) {
    json r;
    
    if (!g_core) { r["error"] = "No core"; return r; }
    
    IAIMPServicePlaylistManager* mgr = nullptr;
    if (g_core->QueryInterface(IID_IAIMPServicePlaylistManager, (void**)&mgr) != S_OK || !mgr) {
        r["error"] = "No playlist manager";
        return r;
    }
    
    IAIMPPlaylist* pl = nullptr;
    if (mgr->GetLoadedPlaylist(playlistId, &pl) != S_OK || !pl) {
        mgr->Release();
        r["error"]["code"] = "PLAYLIST_NOT_FOUND";
        r["error"]["message"] = "Playlist not found";
        return r;
    }
    
    if (trackId >= pl->GetItemCount()) {
        pl->Release(); mgr->Release();
        r["error"]["code"] = "TRACK_NOT_FOUND";
        r["error"]["message"] = "Track not found";
        return r;
    }
    
    IAIMPPlaylistItem* item = nullptr;
    if (pl->GetItem(trackId, IID_IAIMPPlaylistItem, (void**)&item) == S_OK && item) {
        r["id"] = GenerateId("tr", trackId);
        GetFileInfo(item, r);
        r["position_in_playlist"] = trackId + 1;
        
        int playingIdx = GetPlayingIndex(pl);
        int focusedIdx = GetFocusedIndex(pl);
        
        if (trackId == playingIdx) r["state"] = "playing";
        else if (trackId == focusedIdx) r["state"] = "selected";
        else r["state"] = nullptr;
        
        item->Release();
    }
    
    pl->Release();
    mgr->Release();
    
    return r;
}

// ==========================================
// HTTP парсер
// ==========================================
struct HttpRequest {
    std::string method, path, body;
    std::map<std::string, std::string> params;
    std::map<std::string, std::string> headers;
};

HttpRequest ParseRequest(const std::string& raw) {
    HttpRequest req;
    std::istringstream ss(raw);
    std::string line;
    
    if (std::getline(ss, line)) {
        std::istringstream ls(line);
        ls >> req.method >> req.path;
        
        size_t q = req.path.find('?');
        if (q != std::string::npos) {
            std::string query = req.path.substr(q + 1);
            req.path = req.path.substr(0, q);
            std::istringstream qs(query);
            std::string pair;
            while (std::getline(qs, pair, '&')) {
                size_t e = pair.find('=');
                if (e != std::string::npos)
                    req.params[pair.substr(0, e)] = pair.substr(e + 1);
            }
        }
    }
    
    // Заголовки
    while (std::getline(ss, line) && line != "\r" && !line.empty()) {
        line.erase(line.find('\r'));
        size_t colon = line.find(':');
        if (colon != std::string::npos) {
            std::string key = line.substr(0, colon);
            std::string val = line.substr(colon + 1);
            val.erase(0, val.find_first_not_of(' '));
            req.headers[key] = val;
        }
    }
    
    // Тело
    std::string rest;
    while (std::getline(ss, line)) rest += line + "\n";
    req.body = rest;
    
    return req;
}

std::string UrlDecode(const std::string& str) {
    std::string result;
    for (size_t i = 0; i < str.length(); i++) {
        if (str[i] == '%' && i + 2 < str.length()) {
            int val;
            std::istringstream is(str.substr(i + 1, 2));
            if (is >> std::hex >> val) {
                result += (char)val;
                i += 2;
            } else result += str[i];
        } else if (str[i] == '+') result += ' ';
        else result += str[i];
    }
    return result;
}

// ==========================================
// HTTP Сервер
// ==========================================
void SendResponse(SOCKET client, int code, const json& body) {
    std::string jsonStr = body.dump();
    std::string status = (code == 200) ? "OK" : (code == 201) ? "Created" :
                         (code == 400) ? "Bad Request" : (code == 404) ? "Not Found" :
                         (code == 409) ? "Conflict" : "Internal Server Error";
    
    std::string response = 
        "HTTP/1.1 " + std::to_string(code) + " " + status + "\r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, PUT, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Content-Length: " + std::to_string(jsonStr.length()) + "\r\n"
        "Connection: close\r\n\r\n" + jsonStr;
    
    send(client, response.c_str(), response.length(), 0);
}

// Парсинг пути /api/playlists/:id/tracks/:track_id/action
struct ParsedPath {
    int playlistId = -1;
    int trackId = -1;
    std::string action;
};

ParsedPath ParsePath(const std::string& path) {
    ParsedPath result;
    std::string p = path;
    
    // Убираем /api/
    if (p.find("/api/") == 0) p = p.substr(5);
    
    // /playlists/1/tracks/2/play
    if (p.find("playlists/") == 0) {
        p = p.substr(10);
        size_t slash = p.find('/');
        if (slash != std::string::npos) {
            try { result.playlistId = std::stoi(p.substr(0, slash)); } catch(...) {}
            p = p.substr(slash + 1);
            
            if (p.find("tracks/") == 0) {
                p = p.substr(7);
                slash = p.find('/');
                if (slash != std::string::npos) {
                    try { result.trackId = std::stoi(p.substr(0, slash)); } catch(...) {}
                    result.action = p.substr(slash + 1);
                } else {
                    try { result.trackId = std::stoi(p); } catch(...) {}
                    result.action = "info";
                }
            } else {
                result.action = p.empty() ? "info" : p;
            }
        }
    }
    
    return result;
}

void RunHttpServer() {
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv == INVALID_SOCKET) { WSACleanup(); return; }
    
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(g_port);
    addr.sin_addr.s_addr = INADDR_ANY;
    
    if (bind(srv, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
        closesocket(srv); WSACleanup(); return;
    }
    
    listen(srv, SOMAXCONN);
    g_running = true;
    
    while (g_running) {
        fd_set rs; FD_ZERO(&rs); FD_SET(srv, &rs);
        timeval to{1, 0};
        if (select(0, &rs, 0, 0, &to) <= 0) continue;
        
        SOCKET cl = accept(srv, 0, 0);
        if (cl == INVALID_SOCKET) continue;
        
        char buf[16384];
        int n = recv(cl, buf, sizeof(buf) - 1, 0);
        
        if (n > 0) {
            buf[n] = 0;
            HttpRequest req = ParseRequest(std::string(buf));
            json rsp;
            int code = 200;
            
            std::lock_guard<std::mutex> lock(g_mutex);
            
            // ============ РОУТИНГ ============
            
            // OPTIONS
            if (req.method == "OPTIONS") {
                std::string cors = "HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, POST, PUT, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type\r\nContent-Length: 0\r\n\r\n";
                send(cl, cors.c_str(), cors.length(), 0);
                closesocket(cl); continue;
            }
            
            // GET /api/player/status
            else if (req.path == "/api/player/status" && req.method == "GET") {
                rsp = GetPlayerStatus();
            }
            
            // POST /api/player/play
            else if (req.path == "/api/player/play" && req.method == "POST") {
                IAIMPServicePlayer* player = nullptr;
                if (g_core && g_core->QueryInterface(IID_IAIMPServicePlayer, (void**)&player) == S_OK && player) {
                    player->Resume();
                    int st = player->GetState();
                    rsp["state"] = (st == 1) ? "playing" : (st == 2) ? "paused" : "stopped";
                    player->Release();
                } else { rsp["error"]["code"] = "NO_PLAYER"; rsp["error"]["message"] = "Player not available"; code = 500; }
            }
            
            // POST /api/player/pause
            else if (req.path == "/api/player/pause" && req.method == "POST") {
                IAIMPServicePlayer* player = nullptr;
                if (g_core && g_core->QueryInterface(IID_IAIMPServicePlayer, (void**)&player) == S_OK && player) {
                    player->Pause();
                    int st = player->GetState();
                    rsp["state"] = (st == 2) ? "paused" : "stopped";
                    player->Release();
                } else { rsp["error"]["code"] = "NO_PLAYER"; code = 500; }
            }
            
            // POST /api/player/stop
            else if (req.path == "/api/player/stop" && req.method == "POST") {
                IAIMPServicePlayer* player = nullptr;
                if (g_core && g_core->QueryInterface(IID_IAIMPServicePlayer, (void**)&player) == S_OK && player) {
                    player->Pause(); // В AIMP нет Stop — эмулируем через Pause + SetPosition(0)
                    player->SetPosition(0);
                    rsp["state"] = "stopped";
                    player->Release();
                } else { rsp["error"]["code"] = "NO_PLAYER"; code = 500; }
            }
            
            // POST /api/player/next
            else if (req.path == "/api/player/next" && req.method == "POST") {
                IAIMPServicePlayer* player = nullptr;
                if (g_core && g_core->QueryInterface(IID_IAIMPServicePlayer, (void**)&player) == S_OK && player) {
                    player->GoToNext();
                    rsp["playing_track_id"] = "";
                    player->Release();
                }
            }
            
            // POST /api/player/prev
            else if (req.path == "/api/player/prev" && req.method == "POST") {
                IAIMPServicePlayer* player = nullptr;
                if (g_core && g_core->QueryInterface(IID_IAIMPServicePlayer, (void**)&player) == S_OK && player) {
                    player->GoToPrev();
                    rsp["playing_track_id"] = "";
                    player->Release();
                }
            }
            
            // GET /api/player/volume
            else if (req.path == "/api/player/volume" && req.method == "GET") {
                IAIMPServicePlayer* player = nullptr;
                if (g_core && g_core->QueryInterface(IID_IAIMPServicePlayer, (void**)&player) == S_OK && player) {
                    float vol = 0;
                    player->GetVolume(&vol);
                    rsp["volume"] = (int)(vol * 100);
                    rsp["muted"] = (vol == 0);
                    player->Release();
                }
            }
            
            // PUT /api/player/volume
            else if (req.path == "/api/player/volume" && (req.method == "PUT" || req.method == "POST")) {
                float vol = -1;
                if (req.params.count("volume")) vol = std::stof(req.params["volume"]) / 100.0f;
                if (vol < 0 && !req.body.empty()) {
                    try { json b = json::parse(req.body); if (b.contains("volume")) vol = b["volume"].get<float>() / 100.0f; } catch(...) {}
                }
                if (vol >= 0 && vol <= 1) {
                    IAIMPServicePlayer* player = nullptr;
                    if (g_core && g_core->QueryInterface(IID_IAIMPServicePlayer, (void**)&player) == S_OK && player) {
                        player->SetVolume(vol);
                        rsp["volume"] = (int)(vol * 100);
                        player->Release();
                    }
                } else { rsp["error"]["code"] = "INVALID_VOLUME"; rsp["error"]["message"] = "Volume must be 0-100"; code = 400; }
            }
            
            // POST /api/player/mute
            else if (req.path == "/api/player/mute" && req.method == "POST") {
                IAIMPServicePlayer* player = nullptr;
                if (g_core && g_core->QueryInterface(IID_IAIMPServicePlayer, (void**)&player) == S_OK && player) {
                    float vol = 0;
                    player->GetVolume(&vol);
                    // Toggle mute
                    static float savedVolume = 0.5f;
                    if (vol > 0) { savedVolume = vol; player->SetVolume(0); rsp["muted"] = true; }
                    else { player->SetVolume(savedVolume); rsp["muted"] = false; }
                    player->Release();
                }
            }
            
            // PUT /api/player/position
            else if (req.path == "/api/player/position" && (req.method == "PUT" || req.method == "POST")) {
                double pos = -1;
                if (req.params.count("position")) pos = std::stod(req.params["position"]);
                if (pos < 0 && !req.body.empty()) {
                    try { json b = json::parse(req.body); if (b.contains("position")) pos = b["position"]; } catch(...) {}
                }
                if (pos >= 0) {
                    IAIMPServicePlayer* player = nullptr;
                    if (g_core && g_core->QueryInterface(IID_IAIMPServicePlayer, (void**)&player) == S_OK && player) {
                        player->SetPosition(pos);
                        rsp["position"] = pos;
                        player->Release();
                    }
                } else { rsp["error"]["code"] = "INVALID_POSITION"; code = 400; }
            }
            
            // GET /api/playlists
            else if (req.path == "/api/playlists" && req.method == "GET") {
                rsp = GetPlaylistsResponse();
            }
            
            // /api/playlists/:id/...
            else if (req.path.find("/api/playlists/") == 0) {
                ParsedPath pp = ParsePath(req.path);
                
                if (pp.playlistId >= 0 && pp.trackId < 0) {
                    // Действия с плейлистом
                    if (pp.action == "select" && req.method == "POST") {
                        IAIMPServicePlaylistManager* mgr = nullptr;
                        if (g_core && g_core->QueryInterface(IID_IAIMPServicePlaylistManager, (void**)&mgr) == S_OK && mgr) {
                            IAIMPPlaylist* pl = nullptr;
                            if (mgr->GetLoadedPlaylist(pp.playlistId, &pl) == S_OK && pl) {
                                mgr->SetActivePlaylist(pl);
                                rsp["id"] = GenerateId("pl", pp.playlistId);
                                rsp["state"] = "selected";
                                pl->Release();
                            }
                            mgr->Release();
                        }
                    }
                    else if (pp.action == "play" && req.method == "POST") {
                        IAIMPServicePlaylistManager* mgr = nullptr;
                        if (g_core && g_core->QueryInterface(IID_IAIMPServicePlaylistManager, (void**)&mgr) == S_OK && mgr) {
                            IAIMPPlaylist* pl = nullptr;
                            if (mgr->GetLoadedPlaylist(pp.playlistId, &pl) == S_OK && pl) {
                                mgr->SetActivePlaylist(pl);
                                // Запускаем первый трек
                                IAIMPPlaylistItem* item = nullptr;
                                if (pl->GetItem(0, IID_IAIMPPlaylistItem, (void**)&item) == S_OK && item) {
                                    IAIMPServicePlayer* player = nullptr;
                                    if (g_core && g_core->QueryInterface(IID_IAIMPServicePlayer, (void**)&player) == S_OK && player) {
                                        player->Play2(item);
                                        player->Release();
                                    }
                                    item->Release();
                                }
                                rsp["id"] = GenerateId("pl", pp.playlistId);
                                rsp["state"] = "playing";
                                rsp["playing_track_id"] = GenerateId("tr", 0);
                                pl->Release();
                            }
                            mgr->Release();
                        }
                    }
                    else if (pp.action == "tracks" && req.method == "GET") {
                        int limit = 50, offset = 0;
                        if (req.params.count("limit")) limit = std::stoi(req.params["limit"]);
                        if (req.params.count("offset")) offset = std::stoi(req.params["offset"]);
                        rsp = GetTracksResponse(pp.playlistId, limit, offset);
                    }
                    else if (pp.action == "info" && req.method == "GET") {
                        rsp = GetPlaylistResponse(pp.playlistId);
                    }
                }
                else if (pp.playlistId >= 0 && pp.trackId >= 0) {
                    // Действия с треком
                    if (pp.action == "select" && req.method == "POST") {
                        // Установка фокуса на трек
                        IAIMPServicePlaylistManager* mgr = nullptr;
                        if (g_core && g_core->QueryInterface(IID_IAIMPServicePlaylistManager, (void**)&mgr) == S_OK && mgr) {
                            IAIMPPlaylist* pl = nullptr;
                            if (mgr->GetLoadedPlaylist(pp.playlistId, &pl) == S_OK && pl) {
                                IAIMPPlaylistProperties* props = GetPlaylistProps(pl);
                                if (props) {
                                    props->SetValueAsInt32(AIMP_PLAYLIST_PROPID_FOCUSINDEX, pp.trackId);
                                    props->Release();
                                }
                                rsp["id"] = GenerateId("tr", pp.trackId);
                                rsp["state"] = "selected";
                                pl->Release();
                            }
                            mgr->Release();
                        }
                    }
                    else if (pp.action == "play" && req.method == "POST") {
                        IAIMPServicePlaylistManager* mgr = nullptr;
                        if (g_core && g_core->QueryInterface(IID_IAIMPServicePlaylistManager, (void**)&mgr) == S_OK && mgr) {
                            IAIMPPlaylist* pl = nullptr;
                            if (mgr->GetLoadedPlaylist(pp.playlistId, &pl) == S_OK && pl) {
                                IAIMPPlaylistItem* item = nullptr;
                                if (pl->GetItem(pp.trackId, IID_IAIMPPlaylistItem, (void**)&item) == S_OK && item) {
                                    IAIMPServicePlayer* player = nullptr;
                                    if (g_core && g_core->QueryInterface(IID_IAIMPServicePlayer, (void**)&player) == S_OK && player) {
                                        player->Play2(item);
                                        player->Release();
                                    }
                                    item->Release();
                                }
                                rsp["id"] = GenerateId("tr", pp.trackId);
                                rsp["state"] = "playing";
                                pl->Release();
                            }
                            mgr->Release();
                        }
                    }
                    else if (pp.action == "duration" && req.method == "GET") {
                        json trackInfo = GetTrackResponse(pp.playlistId, pp.trackId);
                        rsp["id"] = GenerateId("tr", pp.trackId);
                        rsp["duration"] = trackInfo.value("duration", 0.0);
                    }
                    else if (pp.action == "info" && req.method == "GET") {
                        rsp = GetTrackResponse(pp.playlistId, pp.trackId);
                    }
                }
            }
            
            // GET /api/
            else if (req.path == "/api/" || req.path == "/api") {
                rsp["name"] = "AIMP HTTP Control API v2.0";
                rsp["documentation"] = "Full REST API for AIMP";
                rsp["endpoints"] = json::array({
                    "/api/player/status", "/api/player/play", "/api/player/pause",
                    "/api/player/stop", "/api/player/next", "/api/player/prev",
                    "/api/player/volume", "/api/player/mute", "/api/player/position",
                    "/api/playlists", "/api/playlists/:id", "/api/playlists/:id/tracks",
                    "/api/playlists/:id/tracks/:id", "/api/playlists/:id/tracks/:id/play",
                    "/api/playlists/:id/tracks/:id/select"
                });
            }
            
            else {
                rsp["error"]["code"] = "NOT_FOUND";
                rsp["error"]["message"] = "Endpoint not found";
                code = 404;
            }
            
            SendResponse(cl, code, rsp);
        }
        closesocket(cl);
    }
    closesocket(srv);
    WSACleanup();
}

// ==========================================
// Плагин AIMP
// ==========================================
class HttpControlPlugin : public IAIMPPlugin {
    LONG ref = 1;
public:
    virtual ~HttpControlPlugin() = default;
    HRESULT WINAPI QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = static_cast<IAIMPPlugin*>(this);
        AddRef();
        return S_OK;
    }
    ULONG WINAPI AddRef() override { return InterlockedIncrement(&ref); }
    ULONG WINAPI Release() override {
        LONG r = InterlockedDecrement(&ref);
        if (r == 0) delete this;
        return r;
    }
    TChar* WINAPI InfoGet(int Index) override {
        static wchar_t n[] = L"AIMP HTTP Control API v2";
        static wchar_t a[] = L"DebianDev";
        static wchar_t d[] = L"Full REST API on port 3553";
        switch (Index) { case 0: return n; case 1: return a; case 2: return d; default: return nullptr; }
    }
    LongWord WINAPI InfoGetCategories() override { return 1; }
    void WINAPI SystemNotification(int, IUnknown*) override {}
    HRESULT WINAPI Initialize(IAIMPCore* core) override {
        g_core = core;
        g_serverThread = std::thread(RunHttpServer);
        g_serverThread.detach();
        return S_OK;
    }
    HRESULT WINAPI Finalize() override {
        g_running = false;
        Sleep(100);
        g_core = nullptr;
        return S_OK;
    }
};

extern "C" __declspec(dllexport) HRESULT WINAPI AIMPPluginGetHeader(IAIMPPlugin** header) {
    if (!header) return E_POINTER;
    *header = new HttpControlPlugin();
    return S_OK;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
