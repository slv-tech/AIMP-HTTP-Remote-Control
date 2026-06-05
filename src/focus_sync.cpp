#include "focus_sync.h"
#include "globals.h"

// ==========================================
// RunInMainThread
// ==========================================
class AIMPMainThreadTask : public IUnknown, public IAIMPTask {
    LONG ref_ = 1;
    std::function<void()> fn_;
public:
    explicit AIMPMainThreadTask(std::function<void()> fn) : fn_(std::move(fn)) {}

    HRESULT WINAPI QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IAIMPTask) {
            *ppv = static_cast<IAIMPTask*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG WINAPI AddRef()  override { return InterlockedIncrement(&ref_); }
    ULONG WINAPI Release() override {
        LONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return r;
    }
    void WINAPI Execute(IAIMPTaskOwner*) override { fn_(); }
};

bool RunInMainThread(std::function<void()> fn) {
    if (!g_core) return false;
    IAIMPServiceThreads* threads = nullptr;
    if (g_core->QueryInterface(IID_IAIMPServiceThreads, (void**)&threads) != S_OK || !threads)
        return false;
    auto* task = new AIMPMainThreadTask(std::move(fn));
    HRESULT hr = threads->ExecuteInMainThread(task, AIMP_SERVICE_THREADS_FLAGS_WAITFOR);
    task->Release();
    threads->Release();
    return hr == S_OK;
}

// ==========================================
// PlaylistProps helpers
// ==========================================
int GetFocusedIndex(IAIMPPlaylist* pl) {
    int idx = -1;
    PlaylistProps p(pl);
    if (p) p->GetValueAsInt32(AIMP_PLAYLIST_PROPID_FOCUSINDEX, &idx);
    return idx;
}

void SetPlaylistFocusAndSelection(IAIMPPlaylist* pl, int trackIdx) {
    if (!pl) return;
    PlaylistProps props(pl);
    if (props) props->SetValueAsInt32(AIMP_PLAYLIST_PROPID_FOCUSINDEX, trackIdx);
    int count = pl->GetItemCount();
    for (int i = 0; i < count; i++) {
        IAIMPPlaylistItem* item = nullptr;
        if (pl->GetItem(i, IID_IAIMPPlaylistItem, (void**)&item) == S_OK && item) {
            item->SetValueAsInt32(AIMP_PLAYLISTITEM_PROPID_SELECTED, (i == trackIdx) ? 1 : 0);
            item->Release();
        }
    }
}

int FindPlaylistIndex(IAIMPPlaylist* targetPl) {
    if (!g_core || !targetPl) return -1;
    IAIMPServicePlaylistManager* mgr = nullptr;
    if (g_core->QueryInterface(IID_IAIMPServicePlaylistManager, (void**)&mgr) != S_OK || !mgr)
        return -1;
    int count = mgr->GetLoadedPlaylistCount();
    int found = -1;
    for (int i = 0; i < count; i++) {
        IAIMPPlaylist* pl = nullptr;
        if (mgr->GetLoadedPlaylist(i, &pl) == S_OK && pl) {
            if (pl == targetPl) { found = i; pl->Release(); break; }
            pl->Release();
        }
    }
    mgr->Release();
    return found;
}

// ==========================================
// Playlist listeners
// ==========================================
struct PlaylistListenerEntry {
    IAIMPPlaylist*        playlist;
    class AIMPPlaylistListener* listener;
};

static std::vector<PlaylistListenerEntry> s_playlistListeners;
static std::mutex                         s_listenersMutex;
static class AIMPPlaylistManagerListener* s_managerListener = nullptr;

class AIMPPlaylistListener : public IAIMPPlaylistListener {
    LONG ref_ = 1;
    IAIMPPlaylist* playlist_;
public:
    explicit AIMPPlaylistListener(IAIMPPlaylist* pl) : playlist_(pl) {}

    HRESULT WINAPI QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IAIMPPlaylistListener) {
            *ppv = static_cast<IAIMPPlaylistListener*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG WINAPI AddRef()  override { return InterlockedIncrement(&ref_); }
    ULONG WINAPI Release() override {
        LONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return r;
    }

    void WINAPI Activated() override {
        int idx = FindPlaylistIndex(playlist_);
        if (idx >= 0) {
            int focusIdx = GetFocusedIndex(playlist_);
            std::lock_guard<std::mutex> lk(g_focusMutex);
            g_focusPlaylistIdx = idx;
            g_focusTrackIdx    = (focusIdx >= 0) ? focusIdx : 0;
        }
    }

    void WINAPI Changed(LongWord Flags) override {
        if (Flags & AIMP_PLAYLIST_NOTIFY_FOCUSINDEX) {
            int plIdx    = FindPlaylistIndex(playlist_);
            if (plIdx < 0) return;
            int focusIdx = GetFocusedIndex(playlist_);
            if (focusIdx < 0) return;
            std::lock_guard<std::mutex> lk(g_focusMutex);
            if (g_focusPlaylistIdx == plIdx)
                g_focusTrackIdx = focusIdx;
        }
    }

    void WINAPI Removed() override {
        std::lock_guard<std::mutex> lk(s_listenersMutex);
        for (auto it = s_playlistListeners.begin(); it != s_playlistListeners.end(); ++it) {
            if (it->playlist == playlist_) {
                s_playlistListeners.erase(it);
                break;
            }
        }
    }
};

void AttachListenerToPlaylist(IAIMPPlaylist* pl) {
    if (!pl) return;
    {
        std::lock_guard<std::mutex> lk(s_listenersMutex);
        for (auto& e : s_playlistListeners)
            if (e.playlist == pl) return;
    }
    auto* listener = new AIMPPlaylistListener(pl);
    if (pl->ListenerAdd(listener) == S_OK) {
        std::lock_guard<std::mutex> lk(s_listenersMutex);
        s_playlistListeners.push_back({pl, listener});
    } else {
        listener->Release();
    }
}

void AttachListenerToAllPlaylists() {
    if (!g_core) return;
    IAIMPServicePlaylistManager* mgr = nullptr;
    if (g_core->QueryInterface(IID_IAIMPServicePlaylistManager, (void**)&mgr) != S_OK || !mgr)
        return;
    int count = mgr->GetLoadedPlaylistCount();
    for (int i = 0; i < count; i++) {
        IAIMPPlaylist* pl = nullptr;
        if (mgr->GetLoadedPlaylist(i, &pl) == S_OK && pl) {
            AttachListenerToPlaylist(pl);
            pl->Release();
        }
    }
    mgr->Release();
}

void DetachAllPlaylistListeners() {
    std::lock_guard<std::mutex> lk(s_listenersMutex);
    for (auto& e : s_playlistListeners) {
        if (e.playlist && e.listener) {
            e.playlist->ListenerRemove(e.listener);
            e.listener->Release();
        }
    }
    s_playlistListeners.clear();
}

class AIMPPlaylistManagerListener : public IAIMPExtensionPlaylistManagerListener {
    LONG ref_ = 1;
public:
    HRESULT WINAPI QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IAIMPExtensionPlaylistManagerListener) {
            *ppv = static_cast<IAIMPExtensionPlaylistManagerListener*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG WINAPI AddRef()  override { return InterlockedIncrement(&ref_); }
    ULONG WINAPI Release() override {
        LONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return r;
    }

    void WINAPI PlaylistActivated(IAIMPPlaylist* Playlist) override {
        int idx = FindPlaylistIndex(Playlist);
        if (idx >= 0) {
            int focusIdx = GetFocusedIndex(Playlist);
            std::lock_guard<std::mutex> lk(g_focusMutex);
            g_focusPlaylistIdx = idx;
            g_focusTrackIdx    = (focusIdx >= 0) ? focusIdx : 0;
        }
    }
    void WINAPI PlaylistAdded(IAIMPPlaylist* Playlist) override {
        AttachListenerToPlaylist(Playlist);
    }
    void WINAPI PlaylistRemoved(IAIMPPlaylist*) override {}
};

void InitFocusSync() {
    if (!g_core) return;
    s_managerListener = new AIMPPlaylistManagerListener();
    s_managerListener->AddRef();
    g_core->RegisterExtension(IID_IAIMPServicePlaylistManager, s_managerListener);

    RunInMainThread([&]() {
        AttachListenerToAllPlaylists();
        IAIMPServicePlaylistManager* mgr = nullptr;
        if (g_core->QueryInterface(IID_IAIMPServicePlaylistManager, (void**)&mgr) != S_OK || !mgr)
            return;
        IAIMPPlaylist* activePl = nullptr;
        if (mgr->GetActivePlaylist(&activePl) == S_OK && activePl) {
            int idx = FindPlaylistIndex(activePl);
            if (idx >= 0) {
                int focusIdx = GetFocusedIndex(activePl);
                std::lock_guard<std::mutex> lk(g_focusMutex);
                g_focusPlaylistIdx = idx;
                g_focusTrackIdx    = (focusIdx >= 0) ? focusIdx : 0;
            }
            activePl->Release();
        }
        mgr->Release();
    });
}

void FinalizeFocusSync() {
    DetachAllPlaylistListeners();
    if (g_core && s_managerListener)
        g_core->UnregisterExtension(s_managerListener);
    if (s_managerListener) {
        s_managerListener->Release();
        s_managerListener = nullptr;
    }
}
