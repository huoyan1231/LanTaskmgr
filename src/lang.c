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
    /* STR_ADD_FAVOR      */ L"Type the address above into your phone's browser.\r\n"
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
                             L"by Gordon Walkedby.",
    /* STR_ERR_PORT       */ L"Port must be between 1 and 65535.",
    /* STR_ERR_BIND       */ L"Cannot bind to port",
    /* STR_STATUS_RUNNING*/ L"\u25b6 Running \u2014 listening on port %d",
    /* STR_STATUS_STOPPED */ L"\u25a0 Stopped",
    /* STR_LBL_PORT      */ L"Port",
    /* STR_LBL_PASSWORD  */ L"Password",
    /* STR_LBL_LANG      */ L"Language",
    /* STR_LBL_BINDIP    */ L"Bind to IP",
    /* STR_BINDIP_HINT   */ L"Leave empty to listen on all interfaces; set a LAN IPv4 to keep the web UI off public networks.",
    /* STR_LBL_STATUS    */ L"Status",
    /* STR_LBL_ADDRESSES */ L"LAN addresses (tap one to copy)",
    /* STR_BTN_START     */ L"Start",
    /* STR_BTN_STOP      */ L"Stop",
    /* STR_BTN_GENPW     */ L"Random",
    /* STR_CHK_AUTO      */ L"Start with Windows",
    /* STR_CHK_HIDDEN    */ L"Start minimised to tray",
    /* STR_NO_ADDRESSES  */ L"(no LAN address found)",
    /* STR_TRAY_SHOW     */ L"Show LanTaskmgr",
    /* STR_TRAY_START    */ L"Start service",
    /* STR_TRAY_STOP     */ L"Stop service",
    /* STR_TRAY_ADDRS    */ L"Refresh addresses",
    /* STR_TRAY_QUIT     */ L"Quit LanTaskmgr",
    /* STR_BTN_EXIT      */ L"Exit",
    /* STR_BTN_HIDE_PW   */ L"Hide",
    /* STR_BTN_SHOW_PW   */ L"Show"
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
    /* STR_ADD_FAVOR      */ L"手动输入上面的地址到手机浏览器。\r\n"
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
                             L"「Run Task Manager On Your Phone」。",
    /* STR_ERR_PORT       */ L"端口必须是 1 到 65535 之间的数字。",
    /* STR_ERR_BIND       */ L"无法绑定到端口",
    /* STR_STATUS_RUNNING*/ L"\u25b6 运行中 \u2014 监听端口 %d",
    /* STR_STATUS_STOPPED */ L"\u25a0 已停止",
    /* STR_LBL_PORT      */ L"端口",
    /* STR_LBL_PASSWORD  */ L"密码",
    /* STR_LBL_LANG      */ L"语言",
    /* STR_LBL_BINDIP    */ L"绑定到IP",
    /* STR_BINDIP_HINT   */ L"留空则监听所有网卡；填写局域网 IPv4 可避免 Web 界面暴露在公网。",
    /* STR_LBL_STATUS    */ L"状态",
    /* STR_LBL_ADDRESSES */ L"局域网地址（点击复制）",
    /* STR_BTN_START     */ L"启动服务",
    /* STR_BTN_STOP      */ L"停止服务",
    /* STR_BTN_GENPW     */ L"随机生成",
    /* STR_CHK_AUTO      */ L"开机自动启动",
    /* STR_CHK_HIDDEN    */ L"启动时最小化到托盘",
    /* STR_NO_ADDRESSES  */ L"（未找到局域网地址）",
    /* STR_TRAY_SHOW     */ L"显示主窗口",
    /* STR_TRAY_START    */ L"启动服务",
    /* STR_TRAY_STOP     */ L"停止服务",
    /* STR_TRAY_ADDRS    */ L"刷新地址列表",
    /* STR_TRAY_QUIT     */ L"退出程序",
    /* STR_BTN_EXIT      */ L"退出",
    /* STR_BTN_HIDE_PW   */ L"隐藏",
    /* STR_BTN_SHOW_PW   */ L"显示"
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
    /* STR_ADD_FAVOR      */ L"手動輸入上面的位址到手機瀏覽器。\r\n"
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
                             L"「Run Task Manager On Your Phone」。",
    /* STR_ERR_PORT       */ L"連接埠必須是 1 到 65535 之間的數字。",
    /* STR_ERR_BIND       */ L"無法綁定到連接埠",
    /* STR_STATUS_RUNNING*/ L"\u25b6 執行中 \u2014 監聽連接埠 %d",
    /* STR_STATUS_STOPPED */ L"\u25a0 已關閉",
    /* STR_LBL_PORT      */ L"連接埠",
    /* STR_LBL_PASSWORD  */ L"密碼",
    /* STR_LBL_LANG      */ L"語言",
    /* STR_LBL_BINDIP    */ L"綁定到IP",
    /* STR_BINDIP_HINT   */ L"留空則監聽所有網卡；填寫區域網路 IPv4 可避免 Web 介面暴露到公網。",
    /* STR_LBL_STATUS    */ L"狀態",
    /* STR_LBL_ADDRESSES */ L"區域網路位址（點擊複製）",
    /* STR_BTN_START     */ L"啟動服務",
    /* STR_BTN_STOP      */ L"停止服務",
    /* STR_BTN_GENPW     */ L"隨機產生",
    /* STR_CHK_AUTO      */ L"開機時自動啟動",
    /* STR_CHK_HIDDEN    */ L"啟動時最小化至通知區",
    /* STR_NO_ADDRESSES  */ L"（找不到區域網路位址）",
    /* STR_TRAY_SHOW     */ L"顯示主視窗",
    /* STR_TRAY_START    */ L"啟動服務",
    /* STR_TRAY_STOP     */ L"停止服務",
    /* STR_TRAY_ADDRS    */ L"重新整理位址清單",
    /* STR_TRAY_QUIT     */ L"結束程式",
    /* STR_BTN_EXIT      */ L"結束",
    /* STR_BTN_HIDE_PW   */ L"隱藏",
    /* STR_BTN_SHOW_PW   */ L"顯示"
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
