#include "lang.h"

/* Order must match the ltm_str_id enum exactly. */

static const WCHAR *const kEN[STR_COUNT] = {
    /* STR_APP_TITLE      */ L"LanTaskmgr",
    /* STR_SETTINGS       */ L"Settings",
    /* STR_LANGUAGE       */ L"Language:",
    /* STR_PORT           */ L"Port:",
    /* STR_PASSWORD       */ L"Password:",
    /* STR_SAVE_RESTART   */ L"Save and restart service",
    /* STR_SERVER         */ L"Service:",
    /* STR_RUNNING        */ L"running",
    /* STR_STOPPED        */ L"stopped",
    /* STR_START          */ L"Start",
    /* STR_STOP           */ L"Stop",
    /* STR_RESTART        */ L"Restart",
    /* STR_EXIT           */ L"Exit",
    /* STR_AUTOSTART      */ L"Start with Windows",
    /* STR_START_HIDDEN   */ L"Start minimised to tray",
    /* STR_CONNECT        */ L"Connect",
    /* STR_OPEN_THIS      */ L"Open this page in your phone's browser:",
    /* STR_ADD_FAVOR      */ L"Scan the code, or type the address by hand.\r\n"
                             L"Bookmark it and enable \"Start with Windows\" so it is\r\n"
                             L"ready the moment your PC stops responding.",
    /* STR_ADDRESS        */ L"Address:",
    /* STR_COPY_URL       */ L"Copy address",
    /* STR_COPIED         */ L"Address copied to the clipboard.",
    /* STR_NO_LAN_IP      */ L"No local network address found.",
    /* STR_PASSWORD_EMPTY */ L"Password cannot be empty.",
    /* STR_PORT_INVALID   */ L"Port must be a number between 1 and 65535.",
    /* STR_SAVED          */ L"Settings saved.",
    /* STR_RESTART_OK     */ L"Service restarted.",
    /* STR_FAIL_TO_START  */ L"Cannot start the HTTP server. Reason:",
    /* STR_SHOW_WINDOW    */ L"Show window",
    /* STR_HIDE_TO_TRAY   */ L"Hide to tray",
    /* STR_TRAY_TIP       */ L"LanTaskmgr - task manager for your phone",
    /* STR_ALREADY_RUNNING*/ L"LanTaskmgr is already running.",
    /* STR_ABOUT          */ L"About",
    /* STR_ABOUT_TEXT     */ L"LanTaskmgr " LTM_VERSION_W L"\r\n\r\n"
                             L"Kill runaway processes on this PC from your phone's browser.\r\n"
                             L"Written in C against the Win32 API only - no runtime, no\r\n"
                             L"third-party libraries, no background services.\r\n\r\n"
                             L"Functional rewrite of \"Run Task Manager On Your Phone\"\r\n"
                             L"by Gordon Walkedby."
};

static const WCHAR *const kCN[STR_COUNT] = {
    /* STR_APP_TITLE      */ L"手机任务管理器",
    /* STR_SETTINGS       */ L"设置",
    /* STR_LANGUAGE       */ L"语言：",
    /* STR_PORT           */ L"端口：",
    /* STR_PASSWORD       */ L"密码：",
    /* STR_SAVE_RESTART   */ L"保存并重启服务",
    /* STR_SERVER         */ L"服务：",
    /* STR_RUNNING        */ L"工作中",
    /* STR_STOPPED        */ L"已关闭",
    /* STR_START          */ L"启动",
    /* STR_STOP           */ L"停止",
    /* STR_RESTART        */ L"重启服务",
    /* STR_EXIT           */ L"退出",
    /* STR_AUTOSTART      */ L"开机自动启动",
    /* STR_START_HIDDEN   */ L"启动时最小化到托盘",
    /* STR_CONNECT        */ L"连接",
    /* STR_OPEN_THIS      */ L"在你的手机浏览器里打开这个页面：",
    /* STR_ADD_FAVOR      */ L"扫描二维码，或者手动输入上面的地址。\r\n"
                             L"建议把页面加入手机浏览器收藏夹，并勾选「开机自动启动」，\r\n"
                             L"这样电脑卡死的时候它已经在等着你了。",
    /* STR_ADDRESS        */ L"地址：",
    /* STR_COPY_URL       */ L"复制地址",
    /* STR_COPIED         */ L"地址已复制到剪贴板。",
    /* STR_NO_LAN_IP      */ L"没有找到局域网地址。",
    /* STR_PASSWORD_EMPTY */ L"密码不能为空！",
    /* STR_PORT_INVALID   */ L"端口必须是 1 到 65535 之间的数字。",
    /* STR_SAVED          */ L"设置已保存。",
    /* STR_RESTART_OK     */ L"成功重启服务！",
    /* STR_FAIL_TO_START  */ L"无法启动 HTTP 服务器。原因：",
    /* STR_SHOW_WINDOW    */ L"显示主窗口",
    /* STR_HIDE_TO_TRAY   */ L"隐藏到托盘",
    /* STR_TRAY_TIP       */ L"手机任务管理器",
    /* STR_ALREADY_RUNNING*/ L"手机任务管理器已经在运行了。",
    /* STR_ABOUT          */ L"关于",
    /* STR_ABOUT_TEXT     */ L"手机任务管理器 " LTM_VERSION_W L"\r\n\r\n"
                             L"用手机浏览器结束这台电脑上卡死的程序。\r\n"
                             L"纯 C + Win32 API 编写，不依赖任何运行时、\r\n"
                             L"第三方库或后台服务。\r\n\r\n"
                             L"功能复刻自 戈登走過去 的\r\n"
                             L"「Run Task Manager On Your Phone」。"
};

static const WCHAR *const kTW[STR_COUNT] = {
    /* STR_APP_TITLE      */ L"手機工作管理員",
    /* STR_SETTINGS       */ L"設定",
    /* STR_LANGUAGE       */ L"語言：",
    /* STR_PORT           */ L"連接埠：",
    /* STR_PASSWORD       */ L"密碼：",
    /* STR_SAVE_RESTART   */ L"儲存並重新啟動服務",
    /* STR_SERVER         */ L"服務：",
    /* STR_RUNNING        */ L"執行中",
    /* STR_STOPPED        */ L"已關閉",
    /* STR_START          */ L"啟動",
    /* STR_STOP           */ L"停止",
    /* STR_RESTART        */ L"重新啟動服務",
    /* STR_EXIT           */ L"結束",
    /* STR_AUTOSTART      */ L"開機時自動啟動",
    /* STR_START_HIDDEN   */ L"啟動時最小化至通知區",
    /* STR_CONNECT        */ L"連線",
    /* STR_OPEN_THIS      */ L"在你的手機瀏覽器裡開啟這個頁面：",
    /* STR_ADD_FAVOR      */ L"掃描 QR Code，或者手動輸入上面的位址。\r\n"
                             L"建議把頁面加入手機瀏覽器的書籤，並勾選「開機時自動啟動」，\r\n"
                             L"這樣電腦當掉的時候它已經在等你了。",
    /* STR_ADDRESS        */ L"位址：",
    /* STR_COPY_URL       */ L"複製位址",
    /* STR_COPIED         */ L"位址已複製到剪貼簿。",
    /* STR_NO_LAN_IP      */ L"找不到區域網路位址。",
    /* STR_PASSWORD_EMPTY */ L"密碼不能為空！",
    /* STR_PORT_INVALID   */ L"連接埠必須是 1 到 65535 之間的數字。",
    /* STR_SAVED          */ L"設定已儲存。",
    /* STR_RESTART_OK     */ L"成功重新啟動服務！",
    /* STR_FAIL_TO_START  */ L"無法啟動 HTTP 伺服器。原因：",
    /* STR_SHOW_WINDOW    */ L"顯示主視窗",
    /* STR_HIDE_TO_TRAY   */ L"隱藏至通知區",
    /* STR_TRAY_TIP       */ L"手機工作管理員",
    /* STR_ALREADY_RUNNING*/ L"手機工作管理員已經在執行了。",
    /* STR_ABOUT          */ L"關於",
    /* STR_ABOUT_TEXT     */ L"手機工作管理員 " LTM_VERSION_W L"\r\n\r\n"
                             L"用手機瀏覽器結束這台電腦上當掉的程式。\r\n"
                             L"純 C + Win32 API 撰寫，不依賴任何執行環境、\r\n"
                             L"第三方函式庫或背景服務。\r\n\r\n"
                             L"功能重製自 戈登走過去 的\r\n"
                             L"「Run Task Manager On Your Phone」。"
};

static const WCHAR *const *const kTables[LTM_LANG_COUNT] = { kEN, kCN, kTW };

const WCHAR *ltm_str_in(ltm_lang_id lang, ltm_str_id id)
{
    const WCHAR *s;
    if (lang < 0 || lang >= LTM_LANG_COUNT) {
        lang = LTM_LANG_EN;
    }
    if (id < 0 || id >= STR_COUNT) {
        return L"";
    }
    s = kTables[lang][id];
    return (s != NULL) ? s : kEN[id];
}

const WCHAR *ltm_str(ltm_str_id id)
{
    return ltm_str_in(g_cfg.lang, id);
}
