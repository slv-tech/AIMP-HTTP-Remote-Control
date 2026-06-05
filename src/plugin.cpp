#include "pch.h"
#include "globals.h"
#include "settings.h"
#include "focus_sync.h"
#include "http_server.h"
#include "options_frame.h"

class HttpControlPlugin : public IAIMPPlugin, public IAIMPOptionsDialogFrame {
    LONG ref_ = 1;
    HttpControlOptionsFrame* frame_ = nullptr;
public:
    HRESULT WINAPI QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown) {
            *ppv = static_cast<IAIMPPlugin*>(this); AddRef(); return S_OK;
        }
        if (riid == IID_IAIMPOptionsDialogFrame) {
            *ppv = static_cast<IAIMPOptionsDialogFrame*>(this); AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG WINAPI AddRef()  override { return InterlockedIncrement(&ref_); }
    ULONG WINAPI Release() override {
        LONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return r;
    }

    // IAIMPPlugin
    TChar* WINAPI InfoGet(int Index) override {
        static wchar_t n[] = L"AIMP HTTP Remote Control Server";
        static wchar_t a[] = L"SLV Tech";
        static wchar_t d[] = L"Remote Server and Full REST API for AIMP";
        switch (Index) {
            case AIMP_PLUGIN_INFO_NAME:              return n;
            case AIMP_PLUGIN_INFO_AUTHOR:            return a;
            case AIMP_PLUGIN_INFO_SHORT_DESCRIPTION: return d;
            default: return nullptr;
        }
    }
    LongWord WINAPI InfoGetCategories() override { return AIMP_PLUGIN_CATEGORY_ADDONS; }
    void WINAPI SystemNotification(int, IUnknown*) override {}

    HRESULT WINAPI Initialize(IAIMPCore* core) override {
        // Плагин не предназначен для конвертера/редактора тегов — проверяем IAIMPServicePlayer
        IAIMPServicePlayer* player = nullptr;
        if (core->QueryInterface(IID_IAIMPServicePlayer, (void**)&player) != S_OK || !player)
            return E_FAIL;
        player->Release();

        g_core = core;
        LoadSettings();

        // Получаем IAIMPServiceOptionsDialog и передаём во frame чтобы FrameModified работал
        IAIMPServiceOptionsDialog* optsSvc = nullptr;
        core->QueryInterface(IID_IAIMPServiceOptionsDialog, (void**)&optsSvc);
        frame_ = new HttpControlOptionsFrame(optsSvc);
        if (optsSvc) optsSvc->Release();

        // Регистрируем страницу настроек в диалоге Options AIMP
        core->RegisterExtension(IID_IAIMPServiceOptionsDialog,
            static_cast<IAIMPOptionsDialogFrame*>(this));

        InitFocusSync();
        g_serverThread = std::thread(RunHttpServer);
        g_serverThread.detach();
        return S_OK;
    }

    HRESULT WINAPI Finalize() override {
        g_running = false;
        if (g_serverSocket != INVALID_SOCKET) {
            closesocket(g_serverSocket);
            g_serverSocket = INVALID_SOCKET;
        }
        Sleep(300);
        FinalizeFocusSync();
        if (frame_) { delete frame_; frame_ = nullptr; }
        g_core = nullptr;
        return S_OK;
    }

    // IAIMPOptionsDialogFrame — делегируем в frame_
    HRESULT WINAPI GetName(IAIMPString** s) override { return frame_ ? frame_->GetName(s) : E_FAIL; }
    HWND    WINAPI CreateFrame(HWND p)      override { return frame_ ? frame_->CreateFrame(p) : nullptr; }
    void    WINAPI DestroyFrame()           override { if (frame_) frame_->DestroyFrame(); }
    void    WINAPI Notification(int id)     override { if (frame_) frame_->Notification(id); }
};

extern "C" __declspec(dllexport) HRESULT WINAPI AIMPPluginGetHeader(IAIMPPlugin** header) {
    if (!header) return E_POINTER;
    *header = new HttpControlPlugin();
    return S_OK;
}

BOOL WINAPI DllMain(HINSTANCE hInstDLL, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) g_hInstance = hInstDLL;
    return TRUE;
}
