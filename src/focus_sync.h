#pragma once
#include "pch.h"

// Вспомогательная структура для доступа к IAIMPPropertyList плейлиста
struct PlaylistProps {
    IAIMPPropertyList* ptr = nullptr;
    explicit PlaylistProps(IAIMPPlaylist* pl) {
        if (pl) pl->QueryInterface(IID_IAIMPPropertyList, (void**)&ptr);
    }
    ~PlaylistProps() { if (ptr) ptr->Release(); }
    operator bool()                const { return ptr != nullptr; }
    IAIMPPropertyList* operator->() const { return ptr; }
};

int  GetFocusedIndex(IAIMPPlaylist* pl);
void SetPlaylistFocusAndSelection(IAIMPPlaylist* pl, int trackIdx);
int  FindPlaylistIndex(IAIMPPlaylist* targetPl);

void AttachListenerToPlaylist(IAIMPPlaylist* pl);
void AttachListenerToAllPlaylists();
void DetachAllPlaylistListeners();

void InitFocusSync();
void FinalizeFocusSync();

// Выполнить функцию в главном потоке AIMP с ожиданием
bool RunInMainThread(std::function<void()> fn);
