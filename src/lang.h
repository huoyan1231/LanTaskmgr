/*
 * lang.h - UI strings for the desktop window.
 *
 * The original shipped a Languages\*.xml folder that had to be parsed at
 * start-up. Here the three tables are compiled straight into .rdata: no file
 * I/O, no parser, no allocation, and the folder can never go missing.
 */
#ifndef LTM_LANG_H
#define LTM_LANG_H

#include "common.h"
#include "config.h"

typedef enum ltm_str_id {
    STR_APP_TITLE = 0,
    STR_SETTINGS,
    STR_LANGUAGE,
    STR_PORT,
    STR_PASSWORD,
    STR_SAVE_RESTART,
    STR_SERVER,
    STR_RUNNING,
    STR_STOPPED,
    STR_START,
    STR_STOP,
    STR_RESTART,
    STR_EXIT,
    STR_AUTOSTART,
    STR_START_HIDDEN,
    STR_CONNECT,
    STR_OPEN_THIS,
    STR_ADD_FAVOR,
    STR_ADDRESS,
    STR_COPY_URL,
    STR_COPIED,
    STR_NO_LAN_IP,
    STR_PASSWORD_EMPTY,
    STR_PORT_INVALID,
    STR_SAVED,
    STR_RESTART_OK,
    STR_FAIL_TO_START,
    STR_SHOW_WINDOW,
    STR_HIDE_TO_TRAY,
    STR_TRAY_TIP,
    STR_ALREADY_RUNNING,
    STR_ABOUT,
    STR_ABOUT_TEXT,

    /* --- ui.c desktop window --------------------------------------- */
    STR_ERR_PORT,
    STR_ERR_BIND,
    STR_STATUS_RUNNING,
    STR_STATUS_STOPPED,
    STR_LBL_PORT,
    STR_LBL_PASSWORD,
    STR_LBL_LANG,
    STR_LBL_BINDIP,
    STR_BINDIP_HINT,
    STR_LBL_STATUS,
    STR_LBL_ADDRESSES,
    STR_BTN_START,
    STR_BTN_STOP,
    STR_BTN_GENPW,
    STR_CHK_AUTO,
    STR_CHK_HIDDEN,
    STR_NO_ADDRESSES,
    STR_TRAY_SHOW,
    STR_TRAY_START,
    STR_TRAY_STOP,
    STR_TRAY_ADDRS,
    STR_TRAY_QUIT,
    STR_BTN_EXIT,
    STR_BTN_HIDE_PW,   /* label when password is visible  -> click to hide */
    STR_BTN_SHOW_PW,   /* label when password is hidden   -> click to show */
    STR_NOPW_WARN,     /* startup warning when no password is configured */

    STR_COUNT
} ltm_str_id;

/* Returns the string for the currently selected language. Never NULL. */
const WCHAR *ltm_str(ltm_str_id id);
/* Explicit language variant, used when rendering for a specific client. */
const WCHAR *ltm_str_in(ltm_lang_id lang, ltm_str_id id);

#endif /* LTM_LANG_H */
