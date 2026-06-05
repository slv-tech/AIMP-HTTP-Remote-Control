#include "player_api.h"
#include "globals.h"
#include "focus_sync.h"
#include "utils.h"

// ==========================================
// Message API helpers
// ==========================================
bool MsgGetBool(int propertyId) {
    if (!g_core) return false;
    IAIMPServiceMessageDispatcher* msgd = nullptr;
    if (g_core->QueryInterface(IID_IAIMPServiceMessageDispatcher, (void**)&msgd) != S_OK || !msgd)
        return false;
    BOOL val = FALSE;
    msgd->Send(propertyId, AIMP_MSG_PROPVALUE_GET, &val);
    msgd->Release();
    return val != FALSE;
}

bool MsgSetBool(int propertyId, bool value) {
    if (!g_core) return false;
    IAIMPServiceMessageDispatcher* msgd = nullptr;
    if (g_core->QueryInterface(IID_IAIMPServiceMessageDispatcher, (void**)&msgd) != S_OK || !msgd)
        return false;
    BOOL val = value ? TRUE : FALSE;
    msgd->Send(propertyId, AIMP_MSG_PROPVALUE_SET, &val);
    msgd->Release();
    return value;
}

bool MsgToggleBool(int propertyId) {
    return MsgSetBool(propertyId, !MsgGetBool(propertyId));
}

// ==========================================
// Playlist/track JSON helpers
// ==========================================
std::string GetPlaylistName(IAIMPPlaylist* pl) {
    PlaylistProps p(pl);
    if (!p) return "Unknown";
    IAIMPString* s = nullptr;
    if (p->GetValueAsObject(AIMP_PLAYLIST_PROPID_NAME, IID_IAIMPString, (void**)&s) == S_OK && s) {
        std::string r = WStr(s->GetData()); s->Release(); return r;
    }
    return "Unknown";
}

std::string GetPlaylistId(IAIMPPlaylist* pl) {
    PlaylistProps p(pl);
    if (!p) return "";
    IAIMPString* s = nullptr;
    if (p->GetValueAsObject(AIMP_PLAYLIST_PROPID_ID, IID_IAIMPString, (void**)&s) == S_OK && s) {
        std::string r = WStr(s->GetData()); s->Release(); return r;
    }
    return "";
}

int GetPlayingIndex(IAIMPPlaylist* pl) {
    int idx = -1;
    PlaylistProps p(pl);
    if (p) p->GetValueAsInt32(AIMP_PLAYLIST_PROPID_PLAYINGINDEX, &idx);
    return idx;
}

double GetPlaylistDuration(IAIMPPlaylist* pl) {
    double d = 0;
    PlaylistProps p(pl);
    if (p) p->GetValueAsFloat(AIMP_PLAYLIST_PROPID_DURATION, &d);
    return d;
}

void GetFileInfo(IAIMPPlaylistItem* item, json& out) {
    if (!item) return;
    IAIMPString* s = nullptr;
    if (item->GetValueAsObject(AIMP_PLAYLISTITEM_PROPID_FILENAME, IID_IAIMPString, (void**)&s) == S_OK && s) {
        out["file_path"] = WStr(s->GetData()); s->Release(); s = nullptr;
    }
    IAIMPFileInfo* fi = nullptr;
    if (item->GetValueAsObject(AIMP_PLAYLISTITEM_PROPID_FILEINFO, IID_IAIMPFileInfo, (void**)&fi) == S_OK && fi) {
        if (fi->GetValueAsObject(AIMP_FILEINFO_PROPID_TITLE,       IID_IAIMPString, (void**)&s) == S_OK && s) { out["title"]        = WStr(s->GetData()); s->Release(); s = nullptr; }
        if (fi->GetValueAsObject(AIMP_FILEINFO_PROPID_ARTIST,      IID_IAIMPString, (void**)&s) == S_OK && s) { out["artist"]       = WStr(s->GetData()); s->Release(); s = nullptr; }
        if (fi->GetValueAsObject(AIMP_FILEINFO_PROPID_ALBUM,       IID_IAIMPString, (void**)&s) == S_OK && s) { out["album"]        = WStr(s->GetData()); s->Release(); s = nullptr; }
        if (fi->GetValueAsObject(AIMP_FILEINFO_PROPID_DATE,        IID_IAIMPString, (void**)&s) == S_OK && s) { out["year"]         = WStr(s->GetData()); s->Release(); s = nullptr; }
        if (fi->GetValueAsObject(AIMP_FILEINFO_PROPID_GENRE,       IID_IAIMPString, (void**)&s) == S_OK && s) { out["genre"]        = WStr(s->GetData()); s->Release(); s = nullptr; }
        if (fi->GetValueAsObject(AIMP_FILEINFO_PROPID_TRACKNUMBER, IID_IAIMPString, (void**)&s) == S_OK && s) { out["track_number"] = WStr(s->GetData()); s->Release(); s = nullptr; }
        double dur = 0; if (fi->GetValueAsFloat(AIMP_FILEINFO_PROPID_DURATION, &dur) == S_OK) out["duration"] = dur;
        int    br  = 0; if (fi->GetValueAsInt32(AIMP_FILEINFO_PROPID_BITRATE,  &br)  == S_OK) out["bitrate"]  = br;
        fi->Release();
    }
    if (!out.contains("title") || out["title"].get<std::string>().empty()) {
        if (item->GetValueAsObject(AIMP_PLAYLISTITEM_PROPID_DISPLAYTEXT, IID_IAIMPString, (void**)&s) == S_OK && s) {
            out["title"] = WStr(s->GetData()); s->Release();
        }
    }
}

bool FillPlaylistJson(IAIMPServicePlaylistManager* mgr, int idx, json& out) {
    if (idx < 0) return false;
    IAIMPPlaylist* pl = nullptr;
    if (mgr->GetLoadedPlaylist(idx, &pl) != S_OK || !pl) return false;
    out["id"]          = idx;
    out["aimp_id"]     = GetPlaylistId(pl);
    out["name"]        = GetPlaylistName(pl);
    out["track_count"] = pl->GetItemCount();
    pl->Release();
    return true;
}

bool FillTrackJson(IAIMPServicePlaylistManager* mgr, int plIdx, int trackIdx, json& out) {
    if (plIdx < 0 || trackIdx < 0) return false;
    IAIMPPlaylist* pl = nullptr;
    if (mgr->GetLoadedPlaylist(plIdx, &pl) != S_OK || !pl) return false;
    if (trackIdx >= pl->GetItemCount()) { pl->Release(); return false; }
    IAIMPPlaylistItem* item = nullptr;
    if (pl->GetItem(trackIdx, IID_IAIMPPlaylistItem, (void**)&item) != S_OK || !item) {
        pl->Release(); return false;
    }
    out["id"]          = trackIdx;
    out["playlist_id"] = plIdx;
    GetFileInfo(item, out);
    item->Release();
    pl->Release();
    return true;
}

// ==========================================
// REST API responses
// ==========================================
json GetPlayerStatus() {
    json r;
    r["state"]    = "stopped";
    r["volume"]   = 0;
    r["muted"]    = false;
    r["position"] = 0.0;
    r["duration"] = 0.0;
    if (!g_core) return r;

    IAIMPServicePlayer* player = nullptr;
    if (g_core->QueryInterface(IID_IAIMPServicePlayer, (void**)&player) == S_OK && player) {
        int st = player->GetState();
        r["state"] = (st == AIMP_PLAYER_STATE_PLAYING) ? "playing"
                   : (st == AIMP_PLAYER_STATE_PAUSED)  ? "paused" : "stopped";
        double pos = 0; player->GetPosition(&pos); r["position"]  = pos;
        double dur = 0; player->GetDuration(&dur);  r["duration"]  = dur;
        r["remaining"] = (dur > pos) ? (dur - pos) : 0.0;
        float  vol = 0; player->GetVolume(&vol);    r["volume"]    = (int)(vol * 100.0f);
        BOOL muted = FALSE; player->GetMute(&muted); r["muted"]   = (muted != FALSE);
        player->Release();
    }

    r["shuffle"]   = MsgGetBool(AIMP_MSG_PROPERTY_SHUFFLE);
    r["repeat"]    = MsgGetBool(AIMP_MSG_PROPERTY_REPEAT);
    r["auto_jump"] = MsgGetBool(AIMP_MSG_PROPERTY_AUTOJUMP_TO_NEXT_TRACK);
    r["next_track"] = nullptr;

    int focusPlIdx, focusTrIdx;
    {
        std::lock_guard<std::mutex> lk(g_focusMutex);
        focusPlIdx = g_focusPlaylistIdx;
        focusTrIdx = g_focusTrackIdx;
    }

    RunInMainThread([&]() {
        IAIMPServicePlaylistManager* mgr = nullptr;
        if (g_core->QueryInterface(IID_IAIMPServicePlaylistManager, (void**)&mgr) != S_OK || !mgr)
            return;

        int totalPlaylists = mgr->GetLoadedPlaylistCount();
        r["playlist_count"] = totalPlaylists;

        IAIMPPlaylist* playingPl = nullptr;
        if (mgr->GetPlayingPlaylist(&playingPl) == S_OK && playingPl) {
            for (int i = 0; i < totalPlaylists; i++) {
                IAIMPPlaylist* tmp = nullptr;
                if (mgr->GetLoadedPlaylist(i, &tmp) == S_OK && tmp) {
                    bool match = (tmp == playingPl);
                    tmp->Release();
                    if (match) {
                        json pp; FillPlaylistJson(mgr, i, pp); r["playing_playlist"] = pp;
                        int playingIdx = GetPlayingIndex(playingPl);
                        if (playingIdx >= 0) {
                            json pt; FillTrackJson(mgr, i, playingIdx, pt); r["playing_track"] = pt;
                        } else { r["playing_track"] = nullptr; }
                        break;
                    }
                }
            }
            playingPl->Release();
        } else {
            r["playing_playlist"] = nullptr;
            r["playing_track"]    = nullptr;
        }

        if (totalPlaylists > 0) {
            if (focusPlIdx >= totalPlaylists) focusPlIdx = totalPlaylists - 1;
            { std::lock_guard<std::mutex> lk(g_focusMutex); g_focusPlaylistIdx = focusPlIdx; }
            json fp; if (FillPlaylistJson(mgr, focusPlIdx, fp)) r["focus_playlist"] = fp;
            else r["focus_playlist"] = nullptr;

            IAIMPPlaylist* focusPl = nullptr;
            if (mgr->GetLoadedPlaylist(focusPlIdx, &focusPl) == S_OK && focusPl) {
                int trackCount = focusPl->GetItemCount();
                if (focusTrIdx >= trackCount) focusTrIdx = std::max(0, trackCount - 1);
                { std::lock_guard<std::mutex> lk(g_focusMutex); g_focusTrackIdx = focusTrIdx; }
                json ft;
                if (trackCount > 0 && FillTrackJson(mgr, focusPlIdx, focusTrIdx, ft)) r["focus_track"] = ft;
                else r["focus_track"] = nullptr;
                focusPl->Release();
            } else { r["focus_track"] = nullptr; }
        } else {
            r["focus_playlist"] = nullptr;
            r["focus_track"]    = nullptr;
        }

        IAIMPServicePlaybackQueue* queue = nullptr;
        if (g_core->QueryInterface(IID_IAIMPServicePlaybackQueue, (void**)&queue) == S_OK && queue) {
            IAIMPPlaybackQueueItem* nextItem = nullptr;
            if (queue->GetNextTrack(&nextItem) == S_OK && nextItem) {
                IAIMPPlaylistItem* plItem = nullptr;
                if (nextItem->GetValueAsObject(AIMP_PLAYBACKQUEUEITEM_PROPID_PLAYLISTITEM,
                        IID_IAIMPPlaylistItem, (void**)&plItem) == S_OK && plItem) {
                    json nt; GetFileInfo(plItem, nt); r["next_track"] = nt; plItem->Release();
                }
                nextItem->Release();
            }
            queue->Release();
        }
        mgr->Release();
    });
    return r;
}

json FocusPlaylistShift(int delta) {
    json r;
    if (!g_core) { r["error"] = "core not initialized"; return r; }
    RunInMainThread([&]() {
        IAIMPServicePlaylistManager* mgr = nullptr;
        if (g_core->QueryInterface(IID_IAIMPServicePlaylistManager, (void**)&mgr) != S_OK || !mgr) {
            r["error"] = "playlist manager unavailable"; return;
        }
        int total = mgr->GetLoadedPlaylistCount();
        if (total == 0) { mgr->Release(); r["error"] = "no playlists"; return; }
        int newIdx;
        {
            std::lock_guard<std::mutex> lk(g_focusMutex);
            newIdx = ((g_focusPlaylistIdx + delta) % total + total) % total;
            g_focusPlaylistIdx = newIdx;
            g_focusTrackIdx    = 0;
        }
        IAIMPPlaylist* newPl = nullptr;
        if (mgr->GetLoadedPlaylist(newIdx, &newPl) == S_OK && newPl) {
            mgr->SetActivePlaylist(newPl);
            SetPlaylistFocusAndSelection(newPl, 0);
            newPl->Release();
        }
        json fp; FillPlaylistJson(mgr, newIdx, fp); r["focus_playlist"] = fp;
        json ft; if (FillTrackJson(mgr, newIdx, 0, ft)) r["focus_track"] = ft;
        else r["focus_track"] = nullptr;
        mgr->Release();
    });
    return r;
}

json FocusTrackShift(int delta) {
    json r;
    if (!g_core) { r["error"] = "core not initialized"; return r; }
    RunInMainThread([&]() {
        IAIMPServicePlaylistManager* mgr = nullptr;
        if (g_core->QueryInterface(IID_IAIMPServicePlaylistManager, (void**)&mgr) != S_OK || !mgr) {
            r["error"] = "playlist manager unavailable"; return;
        }
        int plIdx;
        { std::lock_guard<std::mutex> lk(g_focusMutex); plIdx = g_focusPlaylistIdx; }
        IAIMPPlaylist* pl = nullptr;
        if (mgr->GetLoadedPlaylist(plIdx, &pl) != S_OK || !pl) {
            mgr->Release(); r["error"] = "playlist not found"; return;
        }
        int total = pl->GetItemCount();
        if (total == 0) { pl->Release(); mgr->Release(); r["error"] = "playlist is empty"; return; }
        int newTrackIdx;
        {
            std::lock_guard<std::mutex> lk(g_focusMutex);
            newTrackIdx = ((g_focusTrackIdx + delta) % total + total) % total;
            g_focusTrackIdx = newTrackIdx;
        }
        SetPlaylistFocusAndSelection(pl, newTrackIdx);
        pl->Release();
        json ft; FillTrackJson(mgr, plIdx, newTrackIdx, ft); r["focus_track"] = ft;
        json fp; FillPlaylistJson(mgr, plIdx, fp); r["focus_playlist"] = fp;
        mgr->Release();
    });
    return r;
}

json GetPlaylistsResponse() {
    json r; r["playlists"] = json::array();
    if (!g_core) { r["error"] = "core not initialized"; return r; }
    bool ok = RunInMainThread([&]() {
        IAIMPServicePlaylistManager* mgr = nullptr;
        if (g_core->QueryInterface(IID_IAIMPServicePlaylistManager, (void**)&mgr) != S_OK || !mgr) {
            r["error"] = "playlist manager unavailable"; return;
        }
        IAIMPPlaylist* activePl  = nullptr; mgr->GetActivePlaylist(&activePl);
        IAIMPPlaylist* playingPl = nullptr; mgr->GetPlayingPlaylist(&playingPl);
        int count = mgr->GetLoadedPlaylistCount();
        for (int i = 0; i < count; i++) {
            IAIMPPlaylist* pl = nullptr;
            if (mgr->GetLoadedPlaylist(i, &pl) != S_OK || !pl) continue;
            json p;
            p["id"]          = i;
            p["aimp_id"]     = GetPlaylistId(pl);
            p["name"]        = GetPlaylistName(pl);
            p["track_count"] = pl->GetItemCount();
            p["duration"]    = GetPlaylistDuration(pl);
            bool isPlaying = (playingPl && playingPl == pl);
            bool isActive  = (activePl  && activePl  == pl);
            if (isPlaying)      p["state"] = (GetPlayingIndex(pl) >= 0) ? "playing" : "active";
            else if (isActive)  p["state"] = "active";
            else                p["state"] = nullptr;
            r["playlists"].push_back(p);
            pl->Release();
        }
        if (activePl)  activePl->Release();
        if (playingPl) playingPl->Release();
        mgr->Release();
    });
    if (!ok) r["error"] = "ExecuteInMainThread failed";
    return r;
}

json GetPlaylistResponse(int idx) {
    json r;
    if (!g_core) { r["error"] = "core not initialized"; return r; }
    bool ok = RunInMainThread([&]() {
        IAIMPServicePlaylistManager* mgr = nullptr;
        if (g_core->QueryInterface(IID_IAIMPServicePlaylistManager, (void**)&mgr) != S_OK || !mgr) {
            r["error"] = "playlist manager unavailable"; return;
        }
        IAIMPPlaylist* pl = nullptr;
        if (mgr->GetLoadedPlaylist(idx, &pl) != S_OK || !pl) {
            mgr->Release(); r["error"]["code"] = "PLAYLIST_NOT_FOUND"; return;
        }
        IAIMPPlaylist* activePl  = nullptr; mgr->GetActivePlaylist(&activePl);
        IAIMPPlaylist* playingPl = nullptr; mgr->GetPlayingPlaylist(&playingPl);
        r["id"]          = idx;
        r["aimp_id"]     = GetPlaylistId(pl);
        r["name"]        = GetPlaylistName(pl);
        r["track_count"] = pl->GetItemCount();
        r["duration"]    = GetPlaylistDuration(pl);
        bool isPlaying = (playingPl && playingPl == pl);
        bool isActive  = (activePl  && activePl  == pl);
        if (isPlaying)      r["state"] = (GetPlayingIndex(pl) >= 0) ? "playing" : "active";
        else if (isActive)  r["state"] = "active";
        else                r["state"] = nullptr;
        pl->Release();
        if (activePl)  activePl->Release();
        if (playingPl) playingPl->Release();
        mgr->Release();
    });
    if (!ok) r["error"] = "ExecuteInMainThread failed";
    return r;
}

json GetTracksResponse(int plIdx, int limit, int offset) {
    json r;
    r["playlist_id"] = plIdx;
    r["tracks"]      = json::array();
    r["offset"]      = offset;
    r["limit"]       = limit;
    if (!g_core) { r["error"] = "core not initialized"; return r; }
    bool ok = RunInMainThread([&]() {
        IAIMPServicePlaylistManager* mgr = nullptr;
        if (g_core->QueryInterface(IID_IAIMPServicePlaylistManager, (void**)&mgr) != S_OK || !mgr) {
            r["error"] = "playlist manager unavailable"; return;
        }
        IAIMPPlaylist* pl = nullptr;
        if (mgr->GetLoadedPlaylist(plIdx, &pl) != S_OK || !pl) {
            mgr->Release(); r["error"]["code"] = "PLAYLIST_NOT_FOUND"; return;
        }
        mgr->Release();
        int total      = pl->GetItemCount();
        int playingIdx = GetPlayingIndex(pl);
        int focusedIdx = GetFocusedIndex(pl);
        r["total"] = total;
        int end = std::min(offset + limit, total);
        for (int i = offset; i < end; i++) {
            IAIMPPlaylistItem* item = nullptr;
            if (pl->GetItem(i, IID_IAIMPPlaylistItem, (void**)&item) != S_OK || !item) continue;
            json t;
            t["id"]                   = i;
            t["position_in_playlist"] = i + 1;
            GetFileInfo(item, t);
            if (i == playingIdx)      t["state"] = "playing";
            else if (i == focusedIdx) t["state"] = "focused";
            else                      t["state"] = nullptr;
            r["tracks"].push_back(t);
            item->Release();
        }
        pl->Release();
    });
    if (!ok) r["error"] = "ExecuteInMainThread failed";
    return r;
}

json GetTrackResponse(int plIdx, int trackIdx) {
    json r;
    if (!g_core) { r["error"] = "core not initialized"; return r; }
    bool ok = RunInMainThread([&]() {
        IAIMPServicePlaylistManager* mgr = nullptr;
        if (g_core->QueryInterface(IID_IAIMPServicePlaylistManager, (void**)&mgr) != S_OK || !mgr) {
            r["error"] = "playlist manager unavailable"; return;
        }
        IAIMPPlaylist* pl = nullptr;
        if (mgr->GetLoadedPlaylist(plIdx, &pl) != S_OK || !pl) {
            mgr->Release(); r["error"]["code"] = "PLAYLIST_NOT_FOUND"; return;
        }
        mgr->Release();
        if (trackIdx < 0 || trackIdx >= pl->GetItemCount()) {
            pl->Release(); r["error"]["code"] = "TRACK_NOT_FOUND"; return;
        }
        IAIMPPlaylistItem* item = nullptr;
        if (pl->GetItem(trackIdx, IID_IAIMPPlaylistItem, (void**)&item) != S_OK || !item) {
            pl->Release(); r["error"]["code"] = "TRACK_NOT_FOUND"; return;
        }
        r["id"]                   = trackIdx;
        r["position_in_playlist"] = trackIdx + 1;
        GetFileInfo(item, r);
        int playingIdx = GetPlayingIndex(pl);
        int focusedIdx = GetFocusedIndex(pl);
        if (trackIdx == playingIdx)      r["state"] = "playing";
        else if (trackIdx == focusedIdx) r["state"] = "focused";
        else                             r["state"] = nullptr;
        item->Release();
        pl->Release();
    });
    if (!ok) r["error"] = "ExecuteInMainThread failed";
    return r;
}

ParsedPath ParsePath(const std::string& path) {
    ParsedPath r;
    std::string p = path;
    if (p.find("/api/") == 0) p = p.substr(5);
    if (p.find("playlists/") != 0) return r;
    p = p.substr(10);
    size_t sl = p.find('/');
    if (sl == std::string::npos) {
        try { r.playlistId = std::stoi(p); } catch (...) {}
        r.action = "info";
        return r;
    }
    try { r.playlistId = std::stoi(p.substr(0, sl)); } catch (...) {}
    p = p.substr(sl + 1);
    if (p.find("tracks/") == 0) {
        p = p.substr(7);
        sl = p.find('/');
        if (sl == std::string::npos) {
            try { r.trackId = std::stoi(p); } catch (...) {}
            r.action = "info";
        } else {
            try { r.trackId = std::stoi(p.substr(0, sl)); } catch (...) {}
            r.action = p.substr(sl + 1);
        }
    } else {
        r.action = p.empty() ? "info" : p;
    }
    return r;
}

void DoPlaylistAction(const ParsedPath& pp, const std::string& /*method*/,
                      const std::string& /*body*/, json& rsp, int& code) {
    RunInMainThread([&]() {
        IAIMPServicePlaylistManager* mgr = nullptr;
        if (!g_core || g_core->QueryInterface(IID_IAIMPServicePlaylistManager, (void**)&mgr) != S_OK || !mgr) {
            rsp["error"]["code"] = "NO_MANAGER"; code = 500; return;
        }
        IAIMPPlaylist* pl = nullptr;
        if (mgr->GetLoadedPlaylist(pp.playlistId, &pl) != S_OK || !pl) {
            mgr->Release(); rsp["error"]["code"] = "PLAYLIST_NOT_FOUND"; code = 404; return;
        }
        if (pp.trackId < 0) {
            if (pp.action == "select") {
                mgr->SetActivePlaylist(pl);
                rsp["id"] = pp.playlistId; rsp["state"] = "active";
            } else if (pp.action == "resume") {
                mgr->SetActivePlaylist(pl);
                IAIMPServicePlayer* player = nullptr;
                if (g_core->QueryInterface(IID_IAIMPServicePlayer, (void**)&player) == S_OK && player) {
                    player->Play3(pl); player->Release();
                }
                rsp["id"] = pp.playlistId; rsp["state"] = "playing";
            } else if (pp.action == "play") {
                mgr->SetActivePlaylist(pl);
                IAIMPServicePlayer* player = nullptr;
                if (g_core->QueryInterface(IID_IAIMPServicePlayer, (void**)&player) == S_OK && player) {
                    // Берём первый трек и воспроизводим его напрямую через Play2
                    IAIMPPlaylistItem* firstItem = nullptr;
                    if (pl->GetItemCount() > 0 &&
                        pl->GetItem(0, IID_IAIMPPlaylistItem, (void**)&firstItem) == S_OK && firstItem) {
                        player->Play2(firstItem);
                        firstItem->Release();
                    } else {
                        player->Play3(pl);  // fallback если плейлист пустой
                    }
                    player->Release();
                }
                rsp["id"] = pp.playlistId; rsp["state"] = "playing";
            }
        } else {
            IAIMPPlaylistItem* item = nullptr;
            if (pl->GetItem(pp.trackId, IID_IAIMPPlaylistItem, (void**)&item) != S_OK || !item) {
                pl->Release(); mgr->Release();
                rsp["error"]["code"] = "TRACK_NOT_FOUND"; code = 404; return;
            }
            if (pp.action == "play") {
                IAIMPServicePlayer* player = nullptr;
                if (g_core->QueryInterface(IID_IAIMPServicePlayer, (void**)&player) == S_OK && player) {
                    player->Play2(item); player->Release();
                }
                rsp["id"] = pp.trackId; rsp["state"] = "playing";
            } else if (pp.action == "select") {
                PlaylistProps props(pl);
                if (props) props->SetValueAsInt32(AIMP_PLAYLIST_PROPID_FOCUSINDEX, pp.trackId);
                rsp["id"] = pp.trackId; rsp["state"] = "focused";
            }
            item->Release();
        }
        pl->Release();
        mgr->Release();
    });
}
