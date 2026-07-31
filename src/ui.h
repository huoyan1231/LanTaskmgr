/*
 * ui.h - Win32 native window, system tray, settings dialog.
 */
#ifndef LTM_UI_H
#define LTM_UI_H

#include <windows.h>

/* Call once from WinMain.  Returns 0 on success, non-zero on failure.
 * The function enters the message loop and only returns after WM_QUIT. */
int ltm_ui_run(HINSTANCE hInstance, int nCmdShow);

#endif /* LTM_UI_H */
