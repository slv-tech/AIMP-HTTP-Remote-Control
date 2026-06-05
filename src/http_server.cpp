#include "http_server.h"
#include "globals.h"
#include "network.h"
#include "player_api.h"
#include "focus_sync.h"

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
    while (std::getline(ss, line) && line != "\r" && !line.empty()) {}
    std::string rest;
    while (std::getline(ss, line)) rest += line + "\n";
    req.body = rest;
    return req;
}

void SendResponse(SOCKET client, int code, const json& body) {
    std::string js = body.dump();
    std::string status = (code==200)?"OK":(code==201)?"Created":(code==400)?"Bad Request":(code==404)?"Not Found":"Internal Server Error";
    std::string resp =
        "HTTP/1.1 " + std::to_string(code) + " " + status + "\r\n"
        "Content-Type: application/json; charset=utf-8\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Access-Control-Allow-Methods: GET, POST, PUT, OPTIONS\r\n"
        "Access-Control-Allow-Headers: Content-Type\r\n"
        "Content-Length: " + std::to_string(js.size()) + "\r\n"
        "Connection: close\r\n\r\n" + js;
    send(client, resp.c_str(), (int)resp.size(), 0);
}

void RunHttpServer() {
    WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa);
    SOCKET srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv == INVALID_SOCKET) { WSACleanup(); return; }
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(g_port);
    if (g_bindMode == 0) {
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    } else if (g_bindMode == 2 && !g_bindIp.empty()) {
        if (InetPtonW(AF_INET, g_bindIp.c_str(), &addr.sin_addr) != 1)
            addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        addr.sin_addr.s_addr = INADDR_ANY;
    }
    if (bind(srv, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) { closesocket(srv); WSACleanup(); return; }
    listen(srv, SOMAXCONN);
    g_serverSocket = srv;
    g_running = true;

    while (g_running) {
        fd_set rs; FD_ZERO(&rs); FD_SET(srv, &rs);
        timeval to{1, 0};
        if (select(0, &rs, nullptr, nullptr, &to) <= 0) continue;
        sockaddr_in clientAddr{};
        int clientLen = sizeof(clientAddr);
        SOCKET cl = accept(srv, (sockaddr*)&clientAddr, &clientLen);
        if (cl == INVALID_SOCKET) continue;

        if (!IsAllowedClient(clientAddr.sin_addr.s_addr)) {
            closesocket(cl); continue;
        }

        char buf[16384];
        int n = recv(cl, buf, sizeof(buf)-1, 0);
        if (n > 0) {
            buf[n] = 0;
            HttpRequest req = ParseRequest(std::string(buf, n));
            json rsp; int code = 200;

            if (req.method == "OPTIONS") {
                std::string cors = "HTTP/1.1 200 OK\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, POST, PUT, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type\r\nContent-Length: 0\r\n\r\n";
                send(cl, cors.c_str(), (int)cors.size(), 0);
                closesocket(cl); continue;
            }
            else if (req.path == "/api/player/status" && req.method == "GET") {
                rsp = GetPlayerStatus();
            }
            else if (req.path == "/api/player/play" && req.method == "POST") {
                IAIMPServicePlayer* p = nullptr;
                if (g_core && g_core->QueryInterface(IID_IAIMPServicePlayer,(void**)&p)==S_OK && p) { p->Resume(); rsp["state"]="playing"; p->Release(); }
            }
            else if (req.path == "/api/player/pause" && req.method == "POST") {
                IAIMPServicePlayer* p = nullptr;
                if (g_core && g_core->QueryInterface(IID_IAIMPServicePlayer,(void**)&p)==S_OK && p) { p->Pause(); rsp["state"]="paused"; p->Release(); }
            }
            else if (req.path == "/api/player/stop" && req.method == "POST") {
                IAIMPServicePlayer* p = nullptr;
                if (g_core && g_core->QueryInterface(IID_IAIMPServicePlayer,(void**)&p)==S_OK && p) { p->Stop(); rsp["state"]="stopped"; p->Release(); }
            }
            else if (req.path == "/api/player/next" && req.method == "POST") {
                IAIMPServicePlayer* p = nullptr;
                if (g_core && g_core->QueryInterface(IID_IAIMPServicePlayer,(void**)&p)==S_OK && p) { p->GoToNext(); p->Release(); rsp["ok"]=true; }
            }
            else if (req.path == "/api/player/prev" && req.method == "POST") {
                IAIMPServicePlayer* p = nullptr;
                if (g_core && g_core->QueryInterface(IID_IAIMPServicePlayer,(void**)&p)==S_OK && p) { p->GoToPrev(); p->Release(); rsp["ok"]=true; }
            }
            else if (req.path == "/api/player/volume" && req.method == "GET") {
                IAIMPServicePlayer* p = nullptr;
                if (g_core && g_core->QueryInterface(IID_IAIMPServicePlayer,(void**)&p)==S_OK && p) {
                    float v=0; BOOL m=FALSE; p->GetVolume(&v); p->GetMute(&m);
                    rsp["volume"]=(int)(v*100); rsp["muted"]=(m!=FALSE); p->Release();
                }
            }
            else if (req.path == "/api/player/volume" && (req.method=="PUT"||req.method=="POST")) {
                float v = -1;
                if (req.params.count("volume")) try { v=std::stof(req.params["volume"])/100.f; } catch(...) {}
                if (v<0 && !req.body.empty()) try { json b=json::parse(req.body); if(b.contains("volume")) v=b["volume"].get<float>()/100.f; } catch(...) {}
                if (v>=0 && v<=1) {
                    IAIMPServicePlayer* p=nullptr;
                    if (g_core && g_core->QueryInterface(IID_IAIMPServicePlayer,(void**)&p)==S_OK && p) { p->SetVolume(v); rsp["volume"]=(int)(v*100); p->Release(); }
                } else { rsp["error"]["code"]="INVALID_VOLUME"; code=400; }
            }
            else if (req.path == "/api/player/mute" && req.method == "POST") {
                IAIMPServicePlayer* p=nullptr;
                if (g_core && g_core->QueryInterface(IID_IAIMPServicePlayer,(void**)&p)==S_OK && p) {
                    BOOL m=FALSE; p->GetMute(&m); p->SetMute(!m); rsp["muted"]=(m==FALSE); p->Release();
                }
            }
            else if (req.path == "/api/player/position" && (req.method=="PUT"||req.method=="POST")) {
                double pos=-1;
                if (req.params.count("position")) try { pos=std::stod(req.params["position"]); } catch(...) {}
                if (pos<0 && !req.body.empty()) try { json b=json::parse(req.body); if(b.contains("position")) pos=b["position"].get<double>(); } catch(...) {}
                if (pos>=0) {
                    IAIMPServicePlayer* p=nullptr;
                    if (g_core && g_core->QueryInterface(IID_IAIMPServicePlayer,(void**)&p)==S_OK && p) { p->SetPosition(pos); rsp["position"]=pos; p->Release(); }
                } else { rsp["error"]["code"]="INVALID_POSITION"; code=400; }
            }
            else if (req.path == "/api/player/shuffle" && req.method == "POST") {
                rsp["shuffle"] = MsgToggleBool(AIMP_MSG_PROPERTY_SHUFFLE);
            }
            else if (req.path == "/api/player/shuffle" && req.method == "GET") {
                rsp["shuffle"] = MsgGetBool(AIMP_MSG_PROPERTY_SHUFFLE);
            }
            else if (req.path == "/api/player/repeat" && req.method == "POST") {
                rsp["repeat"] = MsgToggleBool(AIMP_MSG_PROPERTY_REPEAT);
            }
            else if (req.path == "/api/player/repeat" && req.method == "GET") {
                rsp["repeat"] = MsgGetBool(AIMP_MSG_PROPERTY_REPEAT);
            }
            else if (req.path == "/api/player/auto-jump" && req.method == "POST") {
                rsp["auto_jump"] = MsgToggleBool(AIMP_MSG_PROPERTY_AUTOJUMP_TO_NEXT_TRACK);
            }
            else if (req.path == "/api/player/auto-jump" && req.method == "GET") {
                rsp["auto_jump"] = MsgGetBool(AIMP_MSG_PROPERTY_AUTOJUMP_TO_NEXT_TRACK);
            }
            else if (req.path == "/api/focus/playlist/next" && req.method == "POST") {
                rsp = FocusPlaylistShift(+1);
            }
            else if (req.path == "/api/focus/playlist/prev" && req.method == "POST") {
                rsp = FocusPlaylistShift(-1);
            }
            else if (req.path == "/api/focus/track/next" && req.method == "POST") {
                rsp = FocusTrackShift(+1);
            }
            else if (req.path == "/api/focus/track/prev" && req.method == "POST") {
                rsp = FocusTrackShift(-1);
            }
            else if (req.path == "/api/focus/play" && req.method == "POST") {
                int plIdx, trIdx;
                { std::lock_guard<std::mutex> lk(g_focusMutex); plIdx = g_focusPlaylistIdx; trIdx = g_focusTrackIdx; }
                ParsedPath pp; pp.playlistId = plIdx; pp.trackId = trIdx; pp.action = "play";
                DoPlaylistAction(pp, "POST", "", rsp, code);
            }
            else if (req.path == "/api/focus" && req.method == "GET") {
                int plIdx, trIdx;
                { std::lock_guard<std::mutex> lk(g_focusMutex); plIdx = g_focusPlaylistIdx; trIdx = g_focusTrackIdx; }
                RunInMainThread([&]() {
                    IAIMPServicePlaylistManager* mgr = nullptr;
                    if (g_core->QueryInterface(IID_IAIMPServicePlaylistManager, (void**)&mgr) != S_OK || !mgr) {
                        rsp["error"] = "playlist manager unavailable"; return;
                    }
                    json fp; FillPlaylistJson(mgr, plIdx, fp); rsp["focus_playlist"] = fp;
                    json ft; FillTrackJson(mgr, plIdx, trIdx, ft); rsp["focus_track"] = ft;
                    mgr->Release();
                });
            }
            else if (req.path == "/api/playlists" && req.method == "GET") {
                rsp = GetPlaylistsResponse();
            }
            else if (req.path.find("/api/playlists/") == 0) {
                ParsedPath pp = ParsePath(req.path);
                if (pp.playlistId < 0) {
                    rsp["error"]["code"] = "INVALID_PATH"; code = 400;
                } else if (pp.action == "info" && req.method == "GET" && pp.trackId < 0) {
                    rsp = GetPlaylistResponse(pp.playlistId);
                } else if (pp.action == "tracks" && req.method == "GET" && pp.trackId < 0) {
                    int lim=50, off=0;
                    if (req.params.count("limit"))  try { lim=std::stoi(req.params["limit"]);  } catch(...) {}
                    if (req.params.count("offset")) try { off=std::stoi(req.params["offset"]); } catch(...) {}
                    rsp = GetTracksResponse(pp.playlistId, lim, off);
                } else if (pp.action == "info" && req.method == "GET" && pp.trackId >= 0) {
                    rsp = GetTrackResponse(pp.playlistId, pp.trackId);
                } else if ((pp.action=="play"||pp.action=="resume"||pp.action=="select") && req.method=="POST") {
                    DoPlaylistAction(pp, req.method, req.body, rsp, code);
                } else if (pp.action == "duration" && req.method == "GET" && pp.trackId >= 0) {
                    json ti = GetTrackResponse(pp.playlistId, pp.trackId);
                    rsp["id"] = pp.trackId; rsp["duration"] = ti.value("duration", 0.0);
                } else {
                    rsp["error"]["code"] = "NOT_FOUND"; code = 404;
                }
            }
            else if (req.path == "/api" || req.path == "/api/") {
                rsp["name"] = "AIMP HTTP Control API";
                rsp["endpoints"] = json::array({
                    "GET  /api/player/status",
                    "POST /api/player/play|pause|stop|next|prev|mute",
                    "GET|PUT  /api/player/volume",
                    "PUT  /api/player/position",
                    "GET|POST /api/player/shuffle|repeat|auto-jump",
                    "GET  /api/focus",
                    "POST /api/focus/playlist/next|prev",
                    "POST /api/focus/track/next|prev",
                    "POST /api/focus/play",
                    "GET  /api/playlists",
                    "GET  /api/playlists/:id",
                    "GET  /api/playlists/:id/tracks",
                    "GET  /api/playlists/:id/tracks/:tid",
                    "POST /api/playlists/:id/play|resume|select",
                    "POST /api/playlists/:id/tracks/:tid/play|select"
                });
            }
            else {
                rsp["error"]["code"] = "NOT_FOUND"; code = 404;
            }

            SendResponse(cl, code, rsp);
        }
        closesocket(cl);
    }
    g_serverSocket = INVALID_SOCKET;
    closesocket(srv);
    WSACleanup();
}

void RestartHttpServer() {
    g_running = false;
    if (g_serverSocket != INVALID_SOCKET) {
        closesocket(g_serverSocket);
        g_serverSocket = INVALID_SOCKET;
    }
    Sleep(300);
    g_serverThread = std::thread(RunHttpServer);
    g_serverThread.detach();
}
