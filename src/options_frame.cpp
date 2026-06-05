#include "options_frame.h"
#include "globals.h"
#include "network.h"
#include "settings.h"
#include "http_server.h"
#include "utils.h"
#include "../sdk/apiGUI.h"
#include "../sdk/apiMUI.h"

// ==========================================
// Frame state
// ==========================================
static int          s_port         = 19122;
static int          s_bindMode     = 1;
static std::wstring s_bindIp       = L"";
static bool         s_allowEnabled = false;
static std::string  s_allowList    = "";

static std::vector<NetworkInterface> s_interfaces;

static IAIMPUIForm*         s_form        = nullptr;
static IAIMPUIBaseEdit*     s_portEdit    = nullptr;
static IAIMPUIBaseComboBox* s_bindCombo   = nullptr;
static IAIMPUICheckBox*     s_allowCheck  = nullptr;
static IAIMPUIBaseEdit*     s_allowEdit   = nullptr;
static IAIMPUILabel*        s_allowLbl    = nullptr;
static IAIMPUILabel*        s_statusLbl   = nullptr;
static IAIMPUIPanel*        s_rowAllow    = nullptr;
// Локализуемые контролы
static IAIMPUIGroupBox*     s_grpServer   = nullptr;
static IAIMPUIGroupBox*     s_grpAccess   = nullptr;
static IAIMPUILabel*        s_lblPort     = nullptr;
static IAIMPUILabel*        s_lblIface    = nullptr;
static IAIMPUILabel*        s_lblDesc     = nullptr;  // описание внизу
static IAIMPUILabel*        s_lblNote     = nullptr;  // примечание внизу

// ==========================================
// Вспомогательные функции
// ==========================================
static IAIMPString* MakeStr(const wchar_t* s) {
    IAIMPString* str = nullptr;
    if (g_core && g_core->CreateObject(IID_IAIMPString, (void**)&str) == S_OK && str)
        str->SetData(const_cast<wchar_t*>(s), (int)wcslen(s));
    return str;
}

static IAIMPServiceUI* GetUIService() {
    if (!g_core) return nullptr;
    IAIMPServiceUI* ui = nullptr;
    g_core->QueryInterface(IID_IAIMPServiceUI, (void**)&ui);
    return ui;
}

// Создать контрол через IAIMPServiceUI
template<typename T>
static T* CreateCtrl(IAIMPServiceUI* ui, IAIMPUIForm* form, IUnknown* parent,
                     const wchar_t* name, IUnknown* events, REFIID iid) {
    T* ctrl = nullptr;
    IAIMPString* nameStr = MakeStr(name);
    IAIMPUIWinControl* parentWC = nullptr;
    if (parent) parent->QueryInterface(IID_IAIMPUIWinControl, (void**)&parentWC);
    ui->CreateControl(form, parentWC, nameStr, events, iid, (void**)&ctrl);
    if (nameStr)  nameStr->Release();
    if (parentWC) parentWC->Release();
    return ctrl;
}

// Установить alignment + высоту (для ualTop)
static void SetAlignTop(IAIMPUIControl* ctrl, int height, int marginBottom = 4) {
    if (!ctrl) return;
    TAIMPUIControlPlacement p = {};
    p.Alignment        = ualTop;
    p.Bounds.bottom    = height;
    p.AlignmentMargins = {0, 0, 0, marginBottom};
    ctrl->SetPlacement(p);
}


static std::wstring GetEditText(IAIMPUIBaseEdit* edit) {
    if (!edit) return L"";
    IAIMPString* s = nullptr;
    if (edit->GetValueAsObject(AIMPUI_BASEEDIT_PROPID_TEXT, IID_IAIMPString, (void**)&s) == S_OK && s) {
        std::wstring r(s->GetData()); s->Release(); return r;
    }
    return L"";
}

static void SetEditText(IAIMPUIBaseEdit* edit, const std::wstring& text) {
    if (!edit) return;
    IAIMPString* s = MakeStr(text.c_str());
    if (s) { edit->SetValueAsObject(AIMPUI_BASEEDIT_PROPID_TEXT, s); s->Release(); }
}

static int GetComboIndex(IAIMPUIBaseComboBox* combo) {
    if (!combo) return -1;
    int idx = -1;
    combo->GetValueAsInt32(AIMPUI_COMBOBOX_PROPID_ITEMINDEX, &idx);
    return idx;
}

static void SetComboIndex(IAIMPUIBaseComboBox* combo, int idx) {
    if (!combo) return;
    combo->SetValueAsInt32(AIMPUI_COMBOBOX_PROPID_ITEMINDEX, idx);
}

static bool GetCheckState(IAIMPUICheckBox* chk) {
    if (!chk) return false;
    int v = 0;
    chk->GetValueAsInt32(AIMPUI_CHECKBOX_PROPID_STATE, &v);
    return v == AIMPUI_CHECKSTATE_CHECKED;
}

static void SetCheckState(IAIMPUICheckBox* chk, bool checked) {
    if (!chk) return;
    chk->SetValueAsInt32(AIMPUI_CHECKBOX_PROPID_STATE,
        checked ? AIMPUI_CHECKSTATE_CHECKED : AIMPUI_CHECKSTATE_UNCHECKED);
}

static void SetLabelText(IAIMPUILabel* lbl, const std::wstring& text) {
    if (!lbl) return;
    IAIMPString* s = MakeStr(text.c_str());
    if (s) { lbl->SetValueAsObject(AIMPUI_LABEL_PROPID_TEXT, s); s->Release(); }
}

static void ComboAddString(IAIMPUIBaseComboBox* combo, const wchar_t* text) {
    if (!combo) return;
    IAIMPString* s = MakeStr(text);
    if (s) { combo->Add(s, 0); s->Release(); }
}



static int FindInterfaceComboIndex(const std::wstring& ip) {
    for (int i = 0; i < (int)s_interfaces.size(); ++i)
        if (s_interfaces[i].ip == ip) return 2 + i;
    return -1;
}

// ==========================================
// MUI — читаем строку из .lng файла
// ==========================================
static std::wstring MUIGet(const wchar_t* keyPath, const wchar_t* fallback = L"") {
    if (!g_core) return fallback;
    IAIMPServiceMUI* mui = nullptr;
    if (g_core->QueryInterface(IID_IAIMPServiceMUI, (void**)&mui) != S_OK || !mui)
        return fallback;
    IAIMPString* key = MakeStr(keyPath);
    IAIMPString* val = nullptr;
    std::wstring result = fallback;
    if (key && mui->GetValue(key, &val) == S_OK && val) {
        result = std::wstring(val->GetData(), val->GetLength());
        val->Release();
    }
    if (key) key->Release();
    mui->Release();
    return result;
}

static void ApplyLocalization() {
    if (!s_form) return;

    // GroupBox заголовки
    if (s_grpServer) {
        auto s = MakeStr(MUIGet(L"AimpHttpControl.Options\\GrpServer", L"Server").c_str());
        if (s) { s_grpServer->SetValueAsObject(AIMPUI_GROUPBOX_PROPID_CAPTION, s); s->Release(); }
    }
    if (s_grpAccess) {
        auto s = MakeStr(MUIGet(L"AimpHttpControl.Options\\GrpAccess", L"Access policy").c_str());
        if (s) { s_grpAccess->SetValueAsObject(AIMPUI_GROUPBOX_PROPID_CAPTION, s); s->Release(); }
    }
    // Labels
    if (s_lblPort)    SetLabelText(s_lblPort,  MUIGet(L"AimpHttpControl.Options\\LblPort",      L"Port:"));
    if (s_lblIface)   SetLabelText(s_lblIface,  MUIGet(L"AimpHttpControl.Options\\LblInterface", L"Listen interface:"));
    if (s_allowLbl)   SetLabelText(s_allowLbl,  MUIGet(L"AimpHttpControl.Options\\LblAllowList", L"IP / CIDR list:"));
    // Checkbox
    if (s_allowCheck) {
        auto s = MakeStr(MUIGet(L"AimpHttpControl.Options\\ChkAllowOnly", L"Allow access only from:").c_str());
        if (s) { s_allowCheck->SetValueAsObject(AIMPUI_CHECKBOX_PROPID_CAPTION, s); s->Release(); }
    }
    // Combo items 0 и 1 (Localhost / All interfaces) — обновляем текст
    if (s_bindCombo) {
        // К сожалению IAIMPUIBaseComboBox не предоставляет метод изменения текста элемента после добавления,
        // поэтому пересоздать список невозможно без пересоздания контрола.
        // Это ограничение AIMP UI — локализация комбобокса недоступна после создания.
    }
    // Описание внизу
    if (s_lblDesc) SetLabelText(s_lblDesc, MUIGet(L"AimpHttpControl.Description\\Text",
        L"The plugin sets up a server for remote control of the player using programs such as Bitfocus Companion and similar programs."));
    if (s_lblNote) SetLabelText(s_lblNote, MUIGet(L"AimpHttpControl.Description\\Note",
        L"Please note: This plugin only launches the server. To set up remote control, you must configure the connection in the appropriate management program."));
}

// ==========================================
// Обработчик изменений — активирует кнопку "Применить"
// ==========================================
class ControlChangeEvents : public IAIMPUIChangeEvents {
    LONG ref_ = 1;
    IAIMPOptionsDialogFrame*   frame_ = nullptr;
    IAIMPServiceOptionsDialog* svc_   = nullptr;
public:
    ControlChangeEvents(IAIMPOptionsDialogFrame* f, IAIMPServiceOptionsDialog* s)
        : frame_(f), svc_(s) { if (svc_) svc_->AddRef(); }
    ~ControlChangeEvents() { if (svc_) svc_->Release(); }

    HRESULT WINAPI QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == IID_IAIMPUIChangeEvents) {
            *ppv = static_cast<IAIMPUIChangeEvents*>(this); AddRef(); return S_OK;
        }
        return E_NOINTERFACE;
    }
    ULONG WINAPI AddRef()  override { return InterlockedIncrement(&ref_); }
    ULONG WINAPI Release() override {
        LONG r = InterlockedDecrement(&ref_);
        if (r == 0) delete this;
        return r;
    }

    void WINAPI OnChanged(IUnknown* sender) override {
        if (svc_ && frame_) svc_->FrameModified(frame_);
    }
};

// ==========================================
// Сохранение настроек
// ==========================================
static void ApplySettings() {
    std::wstring portStr = GetEditText(s_portEdit);
    if (!portStr.empty()) {
        try { int p = std::stoi(portStr); if (p >= 1 && p <= 65535) s_port = p; } catch (...) {}
    }
    int sel = GetComboIndex(s_bindCombo);
    if (sel == 0)      { s_bindMode = 0; s_bindIp = L""; }
    else if (sel == 1) { s_bindMode = 1; s_bindIp = L""; }
    else {
        int i = sel - 2;
        if (i >= 0 && i < (int)s_interfaces.size()) { s_bindMode = 2; s_bindIp = s_interfaces[i].ip; }
    }
    s_allowEnabled = GetCheckState(s_allowCheck);
    s_allowList    = WStrToUtf8(GetEditText(s_allowEdit));

    bool needRestart = (s_port != g_port || s_bindMode != g_bindMode || s_bindIp != g_bindIp);
    g_port = s_port; g_bindMode = s_bindMode; g_bindIp = s_bindIp;
    g_allowListEnabled = s_allowEnabled; g_allowList = s_allowList;
    SaveSettings();
    if (needRestart) RestartHttpServer();

    const wchar_t* bstr = (g_bindMode == 0) ? L"127.0.0.1"
        : (g_bindMode == 2 && !g_bindIp.empty()) ? g_bindIp.c_str() : L"0.0.0.0";
    wchar_t buf[128]; wsprintfW(buf, L"Server: %s:%d", bstr, g_port);
    SetLabelText(s_statusLbl, buf);
}

// ==========================================
// HttpControlOptionsFrame
// ==========================================
HttpControlOptionsFrame::HttpControlOptionsFrame(IAIMPServiceOptionsDialog* svc) : svcOpts_(svc) {
    if (svcOpts_) svcOpts_->AddRef();
}
HttpControlOptionsFrame::~HttpControlOptionsFrame() {
    if (svcOpts_) svcOpts_->Release();
}

HRESULT WINAPI HttpControlOptionsFrame::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_IUnknown || riid == IID_IAIMPOptionsDialogFrame) {
        *ppv = static_cast<IAIMPOptionsDialogFrame*>(this); AddRef(); return S_OK;
    }
    return E_NOINTERFACE;
}
ULONG WINAPI HttpControlOptionsFrame::AddRef()  { return InterlockedIncrement(&ref_); }
ULONG WINAPI HttpControlOptionsFrame::Release() {
    LONG r = InterlockedDecrement(&ref_);
    if (r == 0) delete this;
    return r;
}

HRESULT WINAPI HttpControlOptionsFrame::GetName(IAIMPString** s) {
    if (!s) return E_POINTER;
    IAIMPString* str = MakeStr(L"HTTP Remote Control Server");
    if (str) { *s = str; return S_OK; }
    return E_FAIL;
}

HWND WINAPI HttpControlOptionsFrame::CreateFrame(HWND parentWnd) {
    IAIMPServiceUI* ui = GetUIService();
    if (!ui) return nullptr;

    s_port = g_port; s_bindMode = g_bindMode; s_bindIp = g_bindIp;
    s_allowEnabled = g_allowListEnabled; s_allowList = g_allowList;
    s_interfaces   = EnumNetworkInterfaces();

    // --- Форма ---
    IAIMPString* name = MakeStr(L"HttpCtrlFrame");
    HRESULT hr = ui->CreateForm(parentWnd, AIMPUI_SERVICE_CREATEFORM_FLAGS_CHILD, name, nullptr, &s_form);
    name->Release();
    if (FAILED(hr) || !s_form) { ui->Release(); return nullptr; }

    // Форма занимает всё доступное пространство
    { TAIMPUIControlPlacement p = {}; p.Alignment = ualClient; s_form->SetPlacement(p); }

    auto* ev = new ControlChangeEvents(this, svcOpts_);

    // --- GroupBox "Server" ---
    s_grpServer = CreateCtrl<IAIMPUIGroupBox>(ui, s_form, s_form, L"grpServer", nullptr, IID_IAIMPUIGroupBox);
    IAIMPUIGroupBox* grpServer = s_grpServer;
    if (grpServer) {
        SetAlignTop(grpServer, 0, 8);
        grpServer->SetValueAsInt32(AIMPUI_GROUPBOX_PROPID_AUTOSIZE, 1);
        IAIMPString* cap = MakeStr(L"Server"); grpServer->SetValueAsObject(AIMPUI_GROUPBOX_PROPID_CAPTION, cap); cap->Release();

        // Строка: Listen interface
        IAIMPUIPanel* rowBind = CreateCtrl<IAIMPUIPanel>(ui, s_form, grpServer, L"rowBind", nullptr, IID_IAIMPUIPanel);
        if (rowBind) {
            SetAlignTop(rowBind, 28, 4);
            rowBind->SetValueAsInt32(AIMPUI_PANEL_PROPID_BORDERS, AIMPUI_FLAGS_BORDERS_NONE);

            s_lblIface = CreateCtrl<IAIMPUILabel>(ui, s_form, rowBind, L"lblBind", nullptr, IID_IAIMPUILabel);
            if (s_lblIface) {
                TAIMPUIControlPlacement p = {}; p.Alignment = ualLeft; p.Bounds.right = 130;
                s_lblIface->SetPlacement(p);
                SetLabelText(s_lblIface, L"Listen interface:");
            }
            s_bindCombo = CreateCtrl<IAIMPUIComboBox>(ui, s_form, rowBind, L"bindCombo", ev, IID_IAIMPUIComboBox);
            if (s_bindCombo) {
                TAIMPUIControlPlacement p = {}; p.Alignment = ualClient;
                s_bindCombo->SetPlacement(p);
                s_bindCombo->SetValueAsInt32(AIMPUI_COMBOBOX_PROPID_STYLE, AIMPUI_COMBOBOX_STYLE_LIST);
                ComboAddString(s_bindCombo, L"Localhost (127.0.0.1)");
                ComboAddString(s_bindCombo, L"All interfaces (0.0.0.0)");
                for (const auto& iface : s_interfaces)
                    ComboAddString(s_bindCombo, iface.displayName.c_str());
                if (s_bindMode == 0)      SetComboIndex(s_bindCombo, 0);
                else if (s_bindMode == 2 && !s_bindIp.empty()) {
                    int idx = FindInterfaceComboIndex(s_bindIp);
                    SetComboIndex(s_bindCombo, idx >= 0 ? idx : 1);
                } else                    SetComboIndex(s_bindCombo, 1);
            }
        }

        // Строка: Port
        IAIMPUIPanel* rowPort = CreateCtrl<IAIMPUIPanel>(ui, s_form, grpServer, L"rowPort", nullptr, IID_IAIMPUIPanel);
        if (rowPort) {
            SetAlignTop(rowPort, 28, 4);
            rowPort->SetValueAsInt32(AIMPUI_PANEL_PROPID_BORDERS, AIMPUI_FLAGS_BORDERS_NONE);

            s_lblPort = CreateCtrl<IAIMPUILabel>(ui, s_form, rowPort, L"lblPort", nullptr, IID_IAIMPUILabel);
            if (s_lblPort) {
                TAIMPUIControlPlacement p = {}; p.Alignment = ualLeft; p.Bounds.right = 130;
                s_lblPort->SetPlacement(p);
                SetLabelText(s_lblPort, L"Port:");
            }
            s_portEdit = CreateCtrl<IAIMPUIEdit>(ui, s_form, rowPort, L"portEdit", ev, IID_IAIMPUIEdit);
            if (s_portEdit) {
                TAIMPUIControlPlacement p = {}; p.Alignment = ualClient;
                s_portEdit->SetPlacement(p);
                s_portEdit->SetValueAsInt32(AIMPUI_BASEEDIT_PROPID_MAXLENGTH, 5);
                wchar_t portBuf[16]; wsprintfW(portBuf, L"%d", s_port);
                SetEditText(s_portEdit, portBuf);
            }
        }

        // Строка: статус сервера
        s_statusLbl = CreateCtrl<IAIMPUILabel>(ui, s_form, grpServer, L"statusLbl", nullptr, IID_IAIMPUILabel);
        if (s_statusLbl) {
            SetAlignTop(s_statusLbl, 20, 4);
            const wchar_t* bstr = (g_bindMode == 0) ? L"127.0.0.1"
                : (g_bindMode == 2 && !g_bindIp.empty()) ? g_bindIp.c_str() : L"0.0.0.0";
            wchar_t buf[128]; wsprintfW(buf, L"Server: %s:%d", bstr, g_port);
            SetLabelText(s_statusLbl, buf);
        }

    }

    // --- GroupBox "Access policy" ---
    s_grpAccess = CreateCtrl<IAIMPUIGroupBox>(ui, s_form, s_form, L"grpAccess", nullptr, IID_IAIMPUIGroupBox);
    IAIMPUIGroupBox* grpAccess = s_grpAccess;
    if (grpAccess) {
        SetAlignTop(grpAccess, 0, 8);
        grpAccess->SetValueAsInt32(AIMPUI_GROUPBOX_PROPID_AUTOSIZE, 1);
        IAIMPString* cap = MakeStr(L"Access policy"); grpAccess->SetValueAsObject(AIMPUI_GROUPBOX_PROPID_CAPTION, cap); cap->Release();

        // Checkbox
        s_allowCheck = CreateCtrl<IAIMPUICheckBox>(ui, s_form, grpAccess, L"allowCheck", ev, IID_IAIMPUICheckBox);
        if (s_allowCheck) {
            SetAlignTop(s_allowCheck, 24, 4);
            s_allowCheck->SetValueAsInt32(AIMPUI_CHECKBOX_PROPID_AUTOSIZE, 0);
            IAIMPString* cap2 = MakeStr(L"Allow access only from:");
            s_allowCheck->SetValueAsObject(AIMPUI_CHECKBOX_PROPID_CAPTION, cap2); cap2->Release();
            SetCheckState(s_allowCheck, s_allowEnabled);
        }

        // Строка: IP/CIDR list
        s_rowAllow = CreateCtrl<IAIMPUIPanel>(ui, s_form, grpAccess, L"rowAllow", nullptr, IID_IAIMPUIPanel);
        if (s_rowAllow) {
            SetAlignTop(s_rowAllow, 28, 4);
            s_rowAllow->SetValueAsInt32(AIMPUI_PANEL_PROPID_BORDERS, AIMPUI_FLAGS_BORDERS_NONE);

            s_allowLbl = CreateCtrl<IAIMPUILabel>(ui, s_form, s_rowAllow, L"lblAllow", nullptr, IID_IAIMPUILabel);
            if (s_allowLbl) {
                TAIMPUIControlPlacement p = {}; p.Alignment = ualLeft; p.Bounds.right = 130;
                s_allowLbl->SetPlacement(p);
                SetLabelText(s_allowLbl, L"IP / CIDR list:");
            }
            s_allowEdit = CreateCtrl<IAIMPUIEdit>(ui, s_form, s_rowAllow, L"allowEdit", ev, IID_IAIMPUIEdit);
            if (s_allowEdit) {
                TAIMPUIControlPlacement p = {}; p.Alignment = ualClient;
                s_allowEdit->SetPlacement(p);
                SetEditText(s_allowEdit, Utf8ToWStr(s_allowList));
                IAIMPString* hint = MakeStr(L"192.168.1.0/24, 10.0.0.5");
                if (hint) { s_allowEdit->SetValueAsObject(AIMPUI_EDIT_PROPID_TEXTHINT, hint); hint->Release(); }
                s_allowEdit->SetValueAsInt32(AIMPUI_BASEEDIT_PROPID_MAXLENGTH, 1023);
            }
            // НЕ вызываем Release — держим указатель для управления enabled/release
        }

    }

    // --- Описание внизу ---
    s_lblDesc = CreateCtrl<IAIMPUILabel>(ui, s_form, s_form, L"lblDesc", nullptr, IID_IAIMPUILabel);
    if (s_lblDesc) {
        SetAlignTop(s_lblDesc, 0, 4);
        s_lblDesc->SetValueAsInt32(AIMPUI_LABEL_PROPID_WORDWRAP, 1);
        s_lblDesc->SetValueAsInt32(AIMPUI_LABEL_PROPID_AUTOSIZE, 1);
    }

    s_lblNote = CreateCtrl<IAIMPUILabel>(ui, s_form, s_form, L"lblNote", nullptr, IID_IAIMPUILabel);
    if (s_lblNote) {
        SetAlignTop(s_lblNote, 0, 4);
        s_lblNote->SetValueAsInt32(AIMPUI_LABEL_PROPID_WORDWRAP, 1);
        s_lblNote->SetValueAsInt32(AIMPUI_LABEL_PROPID_AUTOSIZE, 1);
    }

    ApplyLocalization();

    ev->Release();
    ui->Release();
    return s_form->GetHandle();
}

void WINAPI HttpControlOptionsFrame::DestroyFrame() {
    // Дочерние контролы уничтожаются AIMP автоматически вместе с формой.
    // Мы только обнуляем наши указатели чтобы не обращаться к уже уничтоженным объектам.
    s_portEdit   = nullptr;
    s_bindCombo  = nullptr;
    s_allowCheck = nullptr;
    s_rowAllow   = nullptr;
    s_allowEdit  = nullptr;
    s_allowLbl   = nullptr;
    s_statusLbl  = nullptr;
    s_grpServer  = nullptr;
    s_grpAccess  = nullptr;
    s_lblPort    = nullptr;
    s_lblIface   = nullptr;
    s_lblDesc    = nullptr;
    s_lblNote    = nullptr;
    // Форму освобождаем через IAIMPUIForm::Release(BOOL) — как в официальном демо SDK.
    // Release(FALSE) = не postponed, синхронное уничтожение.
    if (s_form) { s_form->Release(FALSE); s_form = nullptr; }
}

void WINAPI HttpControlOptionsFrame::Notification(int id) {
    switch (id) {
    case AIMP_SERVICE_OPTIONSDIALOG_NOTIFICATION_LOCALIZATION:
        ApplyLocalization();
        break;

    case AIMP_SERVICE_OPTIONSDIALOG_NOTIFICATION_LOAD:
        s_port = g_port; s_bindMode = g_bindMode; s_bindIp = g_bindIp;
        s_allowEnabled = g_allowListEnabled; s_allowList = g_allowList;
        if (s_portEdit) { wchar_t b[16]; wsprintfW(b, L"%d", s_port); SetEditText(s_portEdit, b); }
        if (s_bindCombo) {
            if (s_bindMode == 0)      SetComboIndex(s_bindCombo, 0);
            else if (s_bindMode == 2 && !s_bindIp.empty()) {
                int idx = FindInterfaceComboIndex(s_bindIp);
                SetComboIndex(s_bindCombo, idx >= 0 ? idx : 1);
            } else                    SetComboIndex(s_bindCombo, 1);
        }
        if (s_allowCheck) SetCheckState(s_allowCheck, s_allowEnabled);
        if (s_allowEdit)  SetEditText(s_allowEdit, Utf8ToWStr(s_allowList));
        break;

    case AIMP_SERVICE_OPTIONSDIALOG_NOTIFICATION_SAVE:
        ApplySettings();
        break;

    case AIMP_SERVICE_OPTIONSDIALOG_NOTIFICATION_RESET:
        if (s_portEdit)   SetEditText(s_portEdit, L"19122");
        if (s_bindCombo)  SetComboIndex(s_bindCombo, 1);
        if (s_allowCheck) SetCheckState(s_allowCheck, false);
        if (s_allowEdit)  SetEditText(s_allowEdit, L"");
        s_port = 19122; s_bindMode = 1; s_bindIp = L""; s_allowEnabled = false;
        if (svcOpts_) svcOpts_->FrameModified(this);
        break;
    }
}
