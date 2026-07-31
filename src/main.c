/*
 * main.c - WinMain entry point.
 */

#include <windows.h>
#include "ui.h"

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                    LPWSTR lpCmdLine, int nCmdShow)
{
    (void)hPrevInstance;

    /* If the user passes -hidden or --hidden on the command line, start
     * minimised regardless of the saved preference. */
    if (lpCmdLine && (wcsstr(lpCmdLine, L"-hidden") ||
                      wcsstr(lpCmdLine, L"--hidden"))) {
        nCmdShow = SW_HIDE;
    }

    return ltm_ui_run(hInstance, nCmdShow);
}
