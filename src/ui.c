/*
 * ui.c - LanTaskmgr native UI: main window, system tray, settings.
 *
 * Design notes
 * -------------
 * - Single DialogBoxParam-based main window (no separate .rc dialog template;
 *   everything is created programmatically so the only .rc file stays tiny).
 * - System tray icon with a context menu (Show / Start service / Stop service /
 *   Addresses / Quit).
 * - The HTTP server is started/stopped from both the tray menu and the window
 *   UI; state is reflected in both places.
 * - Autostart toggles HKCU\Software\Microsoft\Windows\CurrentVersion\Run.
 * - Single-instance enforcement via a named mutex.
 */

#include "ui.h"
#include "api.h"
#include "assets.h"
#include "config.h"
#include "http.h"
#include "lang.h"
#include "logging.h"
#include "netinfo.h"
#include "procs.h"
#include "resource.h"

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

/* Forward declarations for static functions used before definition. */
static BOOL CALLBACK set_child_font(HWND child, LPARAM font);

#include <commctrl.h>
#include <shellapi.h>

/* ------------------------------------------------------------------ */
/* Constants                                                          */
/* ------------------------------------------------------------------ */

#define LTM_APP_NAME       L"LanTaskmgr"
#define LTM_MUTEX_NAME     L"Local\\LanTaskmgr_SingleInstance"
#define LTM_WM_TRAY        (WM_APP + 0)
#define LTM_WM_REFRESH     (WM_APP + 1)
#define LTM_TIMER_REFRESH  1
#define LTM_REFRESH_MS     3000

#define DLG_W    460
#define DLG_H    470
#define MARGIN   14
#define GAP      8

enum {
    IDC_PORT        = 100,
    IDC_PASSWORD,
    IDC_LANG,
    IDC_BINDIP,
    IDC_AUTOSTART,
    IDC_START,
    IDC_STOP,
    IDC_STATUS,
    IDC_ADDRESSES,
    IDC_GENERATE_PW,
    IDC_TOGGLE_PW,
    IDC_HIDDEN,
    IDC_EXIT
};

static const int LANG_IDS[] = { LTM_LANG_EN, LTM_LANG_CN, LTM_LANG_TW };
static const WCHAR *LANG_LABELS[] = { L"English", L"简体中文", L"繁體中文" };

/* ------------------------------------------------------------------ */
/* State                                                              */
/* ------------------------------------------------------------------ */

static HINSTANCE        g_hinst;
static HWND             g_hwnd = NULL;
static NOTIFYICONDATAW  g_nid = { sizeof(NOTIFYICONDATAW) };
static BOOL             g_server_running = FALSE;

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

static void set_text(HWND parent, int id, const WCHAR *fmt, ...)
{
    WCHAR buf[256];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, _countof(buf), _TRUNCATE, fmt, ap);
    va_end(ap);
    SetDlgItemTextW(parent, id, buf);
}

static void enable_ctrl(HWND dlg, int id, BOOL on)
{
    EnableWindow(GetDlgItem(dlg, id), on);
}

static int get_int(HWND dlg, int id)
{
    return GetDlgItemInt(dlg, id, NULL, FALSE);
}



/* ------------------------------------------------------------------ */
/* Server lifecycle                                                   */
/* ------------------------------------------------------------------ */

static void server_start(HWND dlg)
{
    int port = get_int(dlg, IDC_PORT);
    WCHAR bindip[LTM_BINDIP_MAX];
    if (port < 1 || port > 65535) {
        MessageBoxW(dlg, ltm_str(STR_ERR_PORT), LTM_APP_NAME, MB_ICONWARNING);
        return;
    }

    GetDlgItemTextW(dlg, IDC_BINDIP, bindip, _countof(bindip));
    /* An empty field means "listen on all interfaces" (INADDR_ANY). */

    ltm_proc_init();
    ltm_api_reset();

    if (ltm_http_start((unsigned short)port, bindip[0] ? bindip : NULL)) {
        g_server_running = TRUE;
        set_text(dlg, IDC_STATUS, ltm_str(STR_STATUS_RUNNING));
        enable_ctrl(dlg, IDC_START, FALSE);
        enable_ctrl(dlg, IDC_STOP,  TRUE);
        enable_ctrl(dlg, IDC_PORT,  FALSE);


        SetTimer(dlg, LTM_TIMER_REFRESH, LTM_REFRESH_MS, NULL);
        ltm_log(L"service started on port %d", port);
    } else {
        WCHAR msg[128];
        _snwprintf_s(msg, _countof(msg), _TRUNCATE,
                     L"%s\n\n%s",
                     ltm_str(STR_ERR_BIND),
                     ltm_http_last_error());
        MessageBoxW(dlg, msg, LTM_APP_NAME, MB_ICONERROR);
    }
}

static void server_stop(HWND dlg)
{
    ltm_http_stop();
    ltm_proc_shutdown();
    g_server_running = FALSE;
    KillTimer(dlg, LTM_TIMER_REFRESH);
    set_text(dlg, IDC_STATUS, ltm_str(STR_STATUS_STOPPED));
    enable_ctrl(dlg, IDC_START, TRUE);
    enable_ctrl(dlg, IDC_STOP,  FALSE);
    enable_ctrl(dlg, IDC_PORT,  TRUE);


    ltm_log(L"service stopped");
}

/* ------------------------------------------------------------------ */
/* Address list                                                       */
/* ------------------------------------------------------------------ */

static void refresh_addresses(HWND dlg)
{
    HWND lb = GetDlgItem(dlg, IDC_ADDRESSES);
    ltm_netaddr addrs[16];
    int n, i, port;

    SendMessageW(lb, LB_RESETCONTENT, 0, 0);
    if (!g_server_running) { return; }

    port = get_int(dlg, IDC_PORT);

    /* When bound to a specific interface, only that address is reachable,
     * so don't enumerate every LAN address. */
    if (g_cfg.bind_ip[0] != L'\0') {
        WCHAR entry[128];
        _snwprintf_s(entry, _countof(entry), _TRUNCATE,
                     L"http://%s:%d", g_cfg.bind_ip, port);
        SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)entry);
        return;
    }

    n = ltm_net_list_addresses(addrs, 16);
    for (i = 0; i < n; i++) {
        WCHAR entry[128];
        _snwprintf_s(entry, _countof(entry), _TRUNCATE,
                     L"http://%s:%d%s", addrs[i].ip, port,
                     addrs[i].has_gateway ? L"  \u2605" : L"");
        SendMessageW(lb, LB_ADDSTRING, 0, (LPARAM)entry);
    }
    if (n == 0) {
        SendMessageW(lb, LB_ADDSTRING, 0,
                     (LPARAM)ltm_str(STR_NO_ADDRESSES));
    }
}

/* ------------------------------------------------------------------ */
/* Autostart                                                          */
/* ------------------------------------------------------------------ */

static void autostart_set(BOOL on)
{
    HKEY key;
    WCHAR exe[MAX_PATH];

    if (GetModuleFileNameW(NULL, exe, MAX_PATH) == 0) { return; }

    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) {
        return;
    }

    if (on) {
        RegSetValueExW(key, LTM_APP_NAME, 0, REG_SZ,
                      (const BYTE *)exe,
                      (DWORD)((wcslen(exe) + 1) * sizeof(WCHAR)));
    } else {
        RegDeleteValueW(key, LTM_APP_NAME);
    }
    RegCloseKey(key);
}

static BOOL autostart_is_set(void)
{
    HKEY  key;
    DWORD type, size = 0;
    LONG  r;

    r = RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
                      0, KEY_QUERY_VALUE, &key);
    if (r != ERROR_SUCCESS) { return FALSE; }

    r = RegQueryValueExW(key, LTM_APP_NAME, NULL, &type, NULL, &size);
    RegCloseKey(key);
    return (r == ERROR_SUCCESS && size > 0);
}

/* ------------------------------------------------------------------ */
/* Tray                                                               */
/* ------------------------------------------------------------------ */

static void tray_create(void)
{
    g_nid.cbSize           = sizeof(NOTIFYICONDATAW);
    g_nid.hWnd             = g_hwnd;
    g_nid.uID              = 1;
    /* No NIF_INFO here: that flag belongs to a balloon, and setting it on the
     * initial add with an empty szInfo can swallow the first real notification.
     * tray_notify() sets it per-call instead. */
    g_nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = LTM_WM_TRAY;
    g_nid.hIcon            = LoadIconW(g_hinst, MAKEINTRESOURCEW(IDI_APPICON));
    wcscpy_s(g_nid.szTip, _countof(g_nid.szTip), LTM_APP_NAME);

    Shell_NotifyIconW(NIM_ADD, &g_nid);
}

static void tray_destroy(void)
{
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
}

/* Shows a Windows notification (tray balloon / Action Center toast).
 * Requires the tray icon to already be registered. */
static void tray_notify(const WCHAR *title, const WCHAR *text, DWORD icon)
{
    NOTIFYICONDATAW nid;

    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize   = sizeof(nid);
    nid.hWnd     = g_nid.hWnd;
    nid.uID      = g_nid.uID;
    nid.uFlags   = NIF_INFO;
    nid.dwInfoFlags = icon;
    wcscpy_s(nid.szInfoTitle, _countof(nid.szInfoTitle), title);
    /* szInfo is capped at 256 chars; _TRUNCATE keeps a long localised string
     * from failing the call outright. */
    wcsncpy_s(nid.szInfo, _countof(nid.szInfo), text, _TRUNCATE);

    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

/* Security notice: warn (but do not restrict) when no password is set, so
 * users who deliberately run without one stay in control. Shown once only --
 * the flag is persisted, so it never nags on later launches. */
static void warn_if_no_password(void)
{
    if (g_cfg.password[0] != L'\0' || g_cfg.nopw_warned) {
        return;
    }
    tray_notify(LTM_APP_NAME, ltm_str(STR_NOPW_WARN), NIIF_WARNING);
    g_cfg.nopw_warned = TRUE;
    ltm_config_save(NULL);
}

static void tray_show_menu(void)
{
    POINT pt;
    HMENU menu, popup;
    UINT  flags;

    GetCursorPos(&pt);
    menu = CreatePopupMenu();

    AppendMenuW(menu, MF_STRING, IDM_TRAY_SHOW,   ltm_str(STR_TRAY_SHOW));
    AppendMenuW(menu, MF_SEPARATOR, 0, 0);

    flags = g_server_running ? MF_GRAYED : MF_STRING;
    AppendMenuW(menu, flags, IDM_TRAY_START, ltm_str(STR_TRAY_START));

    flags = g_server_running ? MF_STRING : MF_GRAYED;
    AppendMenuW(menu, flags, IDM_TRAY_STOP,  ltm_str(STR_TRAY_STOP));

    AppendMenuW(menu, MF_SEPARATOR, 0, 0);
    AppendMenuW(menu, MF_STRING, IDM_TRAY_ADDRS, ltm_str(STR_TRAY_ADDRS));
    AppendMenuW(menu, MF_SEPARATOR, 0, 0);
    AppendMenuW(menu, MF_STRING, IDM_TRAY_QUIT,   ltm_str(STR_TRAY_QUIT));

    popup = GetSubMenu(menu, 0);
    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(menu, TPM_RIGHTALIGN, pt.x, pt.y, 0, g_hwnd, NULL);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

/* ------------------------------------------------------------------ */
/* Dialog layout (programmatic)                                       */
/* ------------------------------------------------------------------ */

static HWND make_label(HWND parent, int id, const WCHAR *text, int x, int y,
                        int w, int h)
{
    HWND lbl = CreateWindowExW(0, L"STATIC", text,
                             WS_CHILD | WS_VISIBLE,
                             x, y, w, h, parent, (HMENU)(INT_PTR)id,
                             g_hinst, NULL);
    return lbl;
}

static HWND make_edit(HWND parent, int id, int x, int y, int w, int h)
{
    HWND ed = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                             x, y, w, h, parent, (HMENU)(INT_PTR)id,
                             g_hinst, NULL);
    return ed;
}

static HWND make_button(HWND parent, int id, const WCHAR *text, int x, int y,
                        int w, int h)
{
    HWND btn = CreateWindowExW(0, L"BUTTON", text,
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                 ((id == IDC_START || id == IDC_STOP ||
                                   id == IDC_GENERATE_PW)
                                  ? BS_DEFPUSHBUTTON : 0),
                             x, y, w, h, parent, (HMENU)(INT_PTR)id,
                             g_hinst, NULL);
    return btn;
}

static HWND make_combo(HWND parent, int id, int x, int y, int w, int h)
{
    HWND cbo = CreateWindowExW(0, L"COMBOBOX", L"",
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                 CBS_DROPDOWNLIST | CBS_HASSTRINGS,
                             x, y, w, h, parent, (HMENU)(INT_PTR)id,
                             g_hinst, NULL);
    return cbo;
}

static HWND make_checkbox(HWND parent, int id, const WCHAR *text, int x, int y,
                          int w, int h)
{
    HWND chk = CreateWindowExW(0, L"BUTTON", text,
                             WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                 BS_AUTOCHECKBOX,
                             x, y, w, h, parent, (HMENU)(INT_PTR)id,
                             g_hinst, NULL);
    return chk;
}

static void populate_dialog(HWND dlg)
{
    WCHAR buf[16];

    /* Port */
    _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%d", g_cfg.port);
    SetDlgItemTextW(dlg, IDC_PORT, buf);

    /* Password (masked by default; the toggle button reveals it). */
    {
        HWND hPw = GetDlgItem(dlg, IDC_PASSWORD);
        SendMessageW(hPw, EM_SETPASSWORDCHAR, (WPARAM)L'*', 0);
    }
    SetDlgItemTextW(dlg, IDC_PASSWORD, g_cfg.password);

    /* Bind IP (empty = all interfaces) */
    SetDlgItemTextW(dlg, IDC_BINDIP, g_cfg.bind_ip);

    /* Language combo */
    {
        HWND cb = GetDlgItem(dlg, IDC_LANG);
        int i, sel = 0;
        SendMessageW(cb, CB_RESETCONTENT, 0, 0);
        for (i = 0; i < 3; i++) {
            int idx = (int)SendMessageW(cb, CB_ADDSTRING, 0,
                                        (LPARAM)LANG_LABELS[i]);
            SendMessageW(cb, CB_SETITEMDATA, idx, (LPARAM)LANG_IDS[i]);
            if (LANG_IDS[i] == (int)g_cfg.lang) { sel = idx; }
        }
        SendMessageW(cb, CB_SETCURSEL, sel, 0);
    }

    /* Autostart */
    CheckDlgButton(dlg, IDC_AUTOSTART, autostart_is_set() ? BST_CHECKED
                                                         : BST_UNCHECKED);

    /* Start hidden */
    CheckDlgButton(dlg, IDC_HIDDEN,
                   g_cfg.start_hidden ? BST_CHECKED : BST_UNCHECKED);

    /* Initial button states */
    enable_ctrl(dlg, IDC_START, TRUE);
    enable_ctrl(dlg, IDC_STOP,  FALSE);
    set_text(dlg, IDC_STATUS, ltm_str(STR_STATUS_STOPPED));
}

static void save_from_dialog(HWND dlg)
{
    WCHAR buf[64];
    BOOL ok;

    /* Port */
    GetDlgItemTextW(dlg, IDC_PORT, buf, _countof(buf));
    g_cfg.port = (unsigned short)_wtoi(buf);
    if (g_cfg.port == 0) { g_cfg.port = 9527; }

    /* Password (may be empty: an empty password means the server runs
     * without authentication on the LAN). */
    GetDlgItemTextW(dlg, IDC_PASSWORD, buf, _countof(buf));
    wcsncpy_s(g_cfg.password, _countof(g_cfg.password), buf, _TRUNCATE);

    /* Bind IP (may be empty: an empty value means listen on all interfaces). */
    GetDlgItemTextW(dlg, IDC_BINDIP, buf, _countof(buf));
    wcsncpy_s(g_cfg.bind_ip, _countof(g_cfg.bind_ip), buf, _TRUNCATE);

    /* Language */
    {
        HWND cb = GetDlgItem(dlg, IDC_LANG);
        int idx = (int)SendMessageW(cb, CB_GETCURSEL, 0, 0);
        if (idx >= 0) {
            g_cfg.lang = (ltm_lang_id)SendMessageW(cb, CB_GETITEMDATA, idx, 0);
        }
    }

    /* Autostart */
    autostart_set(IsDlgButtonChecked(dlg, IDC_AUTOSTART) == BST_CHECKED);

    /* Hidden */
    g_cfg.start_hidden = (IsDlgButtonChecked(dlg, IDC_HIDDEN) == BST_CHECKED);

    ltm_config_save(&ok);
}

/* ------------------------------------------------------------------ */
/* Main dialog proc                                                   */
/* ------------------------------------------------------------------ */

static INT_PTR CALLBACK dlg_proc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {

    case WM_INITDIALOG:
        g_hwnd = dlg;
        populate_dialog(dlg);
        refresh_addresses(dlg);
        tray_create();
        warn_if_no_password();

        /* Auto-start the server if configured. */
        if (g_cfg.auto_start_svc) {
            PostMessageW(dlg, WM_COMMAND, IDC_START, 0);
        }
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDOK:
        case IDCANCEL:
            /* Do not close on Enter/Escape — this is a tool window. */
            return 0;

        case IDC_START:
            save_from_dialog(dlg);
            server_start(dlg);
            refresh_addresses(dlg);
            return 0;

        case IDC_STOP:
            server_stop(dlg);
            refresh_addresses(dlg);
            return 0;

        case IDC_GENERATE_PW:
        {
            unsigned char raw[4];
            char hex[9];
            ltm_random_bytes(raw, 4);
            ltm_bytes_to_hex(raw, 4, hex);
            /* Use first 6 chars as numeric password. */
            hex[6] = '\0';
            {
                WCHAR pw[8];
                MultiByteToWideChar(CP_UTF8, 0, hex, -1,
                                   pw, (int)_countof(pw));
                SetDlgItemTextW(dlg, IDC_PASSWORD, pw);
            }
            return 0;
        }

        case IDC_TOGGLE_PW:
        {
            HWND ed = GetDlgItem(dlg, IDC_PASSWORD);
            WPARAM current = SendMessageW(ed, EM_GETPASSWORDCHAR, 0, 0);
            if (current == 0) {
                /* Currently visible → hide with '*'. */
                SendMessageW(ed, EM_SETPASSWORDCHAR, (WPARAM)'*', 0);
                SetDlgItemTextW(dlg, IDC_TOGGLE_PW,
                                ltm_str(STR_BTN_SHOW_PW));
            } else {
                /* Currently hidden → show. */
                SendMessageW(ed, EM_SETPASSWORDCHAR, 0, 0);
                SetDlgItemTextW(dlg, IDC_TOGGLE_PW,
                                ltm_str(STR_BTN_HIDE_PW));
            }
            /* Force repaint so the edit redraws with new style. */
            InvalidateRect(ed, NULL, TRUE);
            return 0;
        }

        case IDM_TRAY_SHOW:
            ShowWindow(dlg, SW_SHOW);
            SetForegroundWindow(dlg);
            return 0;

        case IDM_TRAY_START:
            server_start(dlg);
            refresh_addresses(dlg);
            return 0;

        case IDM_TRAY_STOP:
            server_stop(dlg);
            refresh_addresses(dlg);
            return 0;

        case IDM_TRAY_ADDRS:
            refresh_addresses(dlg);
            ShowWindow(dlg, SW_SHOW);
            SetForegroundWindow(dlg);
            SetFocus(GetDlgItem(dlg, IDC_ADDRESSES));
            return 0;

        case IDM_TRAY_QUIT:
            DestroyWindow(dlg);
            return 0;

        case IDC_EXIT:
            DestroyWindow(dlg);
            return 0;
        }
        break;

    case WM_TIMER:
        if (wp == LTM_TIMER_REFRESH) {
            refresh_addresses(dlg);
        }
        return 0;


    case LTM_WM_TRAY:
        switch (LOWORD(lp)) {
        case WM_RBUTTONUP:
        case WM_CONTEXTMENU:
            tray_show_menu();
            break;
        case WM_LBUTTONDBLCLK:
            ShowWindow(dlg, SW_SHOW);
            SetForegroundWindow(dlg);
            break;
        }
        return 0;

    case WM_CLOSE:
        /* Minimize to tray instead of closing. */
        ShowWindow(dlg, SW_HIDE);
        return 0;

    case WM_DESTROY:
        tray_destroy();
        if (g_server_running) { server_stop(dlg); }
        save_from_dialog(dlg);
        ltm_api_shutdown();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(dlg, msg, wp, lp);
}

/* ------------------------------------------------------------------ */
/* Dialog template (built in code)                                    */
/* ------------------------------------------------------------------ */

/*
 * Layout grid (all coordinates relative to client area):
 *
 *   Row 0:  [Port label]  [Port edit]  [Start btn] [Stop btn]
 *   Row 1:  [Password]    [Password edit]  [Generate]
 *   Row 2:  [Language]    [Combo]
 *   Row 3:  [☐ Autostart] [☐ Start hidden]
 *   Row 4:  --- Status line ---
 *   Row 5:  [Addresses listbox]
 */

static HWND create_main_dialog(HINSTANCE hinst)
{
    HWND dlg, c;
    int y = MARGIN;
    int lw = 90;   /* label width */
    int ew = 120;  /* edit width */
    int bw = 70;   /* button width */
    int bh = 26;   /* button height */
    int eh = 23;   /* edit height */

    dlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE,
        L"LanTaskmgrWCls", LTM_APP_NAME,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, DLG_W, DLG_H,
        NULL, NULL, hinst, NULL);

    /* NOTE: the window uses a real window class (LanTaskmgrWCls) whose
     * WNDPROC is dlg_proc. The built-in #32770 class created via
     * CreateWindowExW does NOT attach a dialog procedure, which would leave
     * the window inert and the process impossible to quit. */

    /* -- Row 0: Port ------------------------------------------------- */
    make_label(dlg, -1, ltm_str(STR_LBL_PORT), MARGIN, y, lw, eh);
    c = make_edit(dlg, IDC_PORT, MARGIN + lw + GAP, y, ew, eh);
    make_button(dlg, IDC_START, ltm_str(STR_BTN_START),
                MARGIN + lw + ew + GAP * 2, y, bw, bh);
    make_button(dlg, IDC_STOP, ltm_str(STR_BTN_STOP),
                MARGIN + lw + ew + GAP * 2 + bw + GAP, y, bw, bh);
    y += eh + GAP + 4;

    /* -- Row 1: Password --------------------------------------------- */
    make_label(dlg, -1, ltm_str(STR_LBL_PASSWORD), MARGIN, y, lw, eh);
    c = make_edit(dlg, IDC_PASSWORD, MARGIN + lw + GAP, y, ew, eh);
    /* Default: password is MASKED (EM_SETPASSWORDCHAR '*', set in
     * populate_dialog). The toggle button reveals/hides it. */
    make_button(dlg, IDC_TOGGLE_PW, ltm_str(STR_BTN_SHOW_PW),
                MARGIN + lw + ew + GAP * 2, y, 28, eh);
    make_button(dlg, IDC_GENERATE_PW, ltm_str(STR_BTN_GENPW),
                MARGIN + lw + ew + GAP * 3 + 28, y, bw + 20, bh);
    y += eh + GAP + 4;

    /* -- Row 2: Language --------------------------------------------- */
    make_label(dlg, -1, ltm_str(STR_LBL_LANG), MARGIN, y, lw, eh);
    make_combo(dlg, IDC_LANG, MARGIN + lw + GAP, y, ew, eh + 80);
    y += eh + GAP + 4;

    /* -- Row 2b: Bind IP --------------------------------------------- */
    make_label(dlg, -1, ltm_str(STR_LBL_BINDIP), MARGIN, y, lw, eh);
    make_edit(dlg, IDC_BINDIP, MARGIN + lw + GAP, y, ew * 2 + GAP, eh);
    y += eh + 2;
    {
        HWND hint = make_label(dlg, -1, ltm_str(STR_BINDIP_HINT),
                               MARGIN + lw + GAP, y, DLG_W - MARGIN * 2 - lw - GAP, eh * 2);
        (void)hint;
    }
    y += eh * 2 + GAP + 4;

    /* -- Row 3: Checkboxes ------------------------------------------- */
    make_checkbox(dlg, IDC_AUTOSTART, ltm_str(STR_CHK_AUTO),
                  MARGIN, y, 140, eh);
    make_checkbox(dlg, IDC_HIDDEN, ltm_str(STR_CHK_HIDDEN),
                  MARGIN + 160, y, 140, eh);
    y += eh + GAP + 10;

    /* -- Row 4: Status ----------------------------------------------- */
    make_label(dlg, -1, ltm_str(STR_LBL_STATUS), MARGIN, y, lw, eh);
    make_label(dlg, IDC_STATUS, L"", MARGIN + lw + GAP, y, 200, eh);
    y += eh + GAP + 6;

    /* -- Row 5: Addresses -------------------------------------------- */
    make_label(dlg, -1, ltm_str(STR_LBL_ADDRESSES), MARGIN, y, lw, eh);
    {
        HWND lb = CreateWindowExW(
            WS_EX_CLIENTEDGE, L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_BORDER | LBS_NOINTEGRALHEIGHT,
            MARGIN, y + eh + 2, DLG_W - MARGIN * 2, 85,
            dlg, (HMENU)(INT_PTR)IDC_ADDRESSES, hinst, NULL);
        (void)lb;
    }
    y += eh + 2 + 85 + GAP;

    /* -- Row 6: Exit ------------------------------------------------ */
    make_button(dlg, IDC_EXIT, ltm_str(STR_BTN_EXIT),
                DLG_W - MARGIN - bw, y, bw, bh);
    y += bh + GAP;


    /* Set font to match the dialog. */
    {
        HFONT hf = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        EnumChildWindows(dlg, set_child_font, (LPARAM)hf);
    }

    /* CreateWindowExW on #32770 never sends WM_INITDIALOG, so fire it
     * ourselves now that every control has been created.
     */
    SendMessageW(dlg, WM_INITDIALOG, 0, 0);

    return dlg;
}

static BOOL CALLBACK set_child_font(HWND child, LPARAM font)
{
    SendMessageW(child, WM_SETFONT, (WPARAM)font, TRUE);
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Public entry point                                                 */
/* ------------------------------------------------------------------ */

int ltm_ui_run(HINSTANCE hInstance, int nCmdShow)
{
    HWND dlg;
    MSG  msg;
    HANDLE mutex;
    int  ret = 0;

    g_hinst = hInstance;

    /* ---- single instance ------------------------------------------ */
    mutex = CreateMutexW(NULL, TRUE, LTM_MUTEX_NAME);
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        /* Bring existing instance to front and exit. */
        HWND other = FindWindowW(L"LanTaskmgrWCls", LTM_APP_NAME);
        if (other) {
            ShowWindow(other, SW_RESTORE);
            SetForegroundWindow(other);
        }
        CloseHandle(mutex);
        return 1;
    }

    /* ---- load config ---------------------------------------------- */
    ltm_config_load();
    ltm_log_init();

    InitCommonControls();

    /* ---- register main window class ------------------------------- */
    {
        WNDCLASSEXW wc;
        ZeroMemory(&wc, sizeof(wc));
        wc.cbSize        = sizeof(wc);
        wc.style         = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc   = (WNDPROC)dlg_proc;
        wc.hInstance     = hInstance;
        wc.hIcon         = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_APPICON));
        wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = L"LanTaskmgrWCls";
        RegisterClassExW(&wc);
    }

    /* ---- create dialog -------------------------------------------- */
    dlg = create_main_dialog(hInstance);
    if (!dlg) {
        ltm_log(L"failed to create main window");
        ret = 2;
        goto cleanup;
    }

    ShowWindow(dlg, g_cfg.start_hidden ? SW_HIDE : nCmdShow);
    UpdateWindow(dlg);

    /* ---- message loop --------------------------------------------- */
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (!IsDialogMessageW(dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    ret = (int)msg.wParam;

cleanup:
    ltm_log_shutdown();
    ReleaseMutex(mutex);
    CloseHandle(mutex);
    return ret;
}
