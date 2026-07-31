/*
 * config.h - persistent settings, stored as a tiny UTF-8 INI file.
 *
 * The original program used an XML serializer; an INI keeps the parser to a
 * few dozen lines and the file human-editable.
 */
#ifndef LTM_CONFIG_H
#define LTM_CONFIG_H

#include "common.h"

#define LTM_PASSWORD_MAX 64
#define LTM_DEFAULT_PORT 5555

typedef enum ltm_lang_id {
    LTM_LANG_EN = 0,
    LTM_LANG_CN = 1,
    LTM_LANG_TW = 2,
    LTM_LANG_COUNT
} ltm_lang_id;

typedef struct ltm_config {
    WCHAR       password[LTM_PASSWORD_MAX];
    int         port;
    ltm_lang_id lang;
    BOOL        autostart;   /* mirrors the HKCU Run registry value */
    BOOL        start_hidden; /* launch minimised to tray */
    BOOL        auto_start_svc; /* start HTTP service immediately on launch */
} ltm_config;

/* The single global configuration instance. */
extern ltm_config g_cfg;

/* Loads settings.ini, filling in defaults for anything missing.
 * Returns FALSE when the file did not exist (first run). */
BOOL ltm_config_load(void);
BOOL ltm_config_save(void);
void ltm_config_defaults(void);

const WCHAR *ltm_lang_code(ltm_lang_id id);      /* "EN" / "CN" / "TW" */
ltm_lang_id  ltm_lang_from_code(const WCHAR *s);
/* Best guess from the user's UI language, used on first run. */
ltm_lang_id  ltm_lang_detect_system(void);

#endif /* LTM_CONFIG_H */
