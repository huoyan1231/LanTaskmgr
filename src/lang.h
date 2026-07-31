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
    STR_COUNT
} ltm_str_id;

/* Returns the string for the currently selected language. Never NULL. */
const WCHAR *ltm_str(ltm_str_id id);
/* Explicit language variant, used when rendering for a specific client. */
const WCHAR *ltm_str_in(ltm_lang_id lang, ltm_str_id id);

#endif /* LTM_LANG_H */
