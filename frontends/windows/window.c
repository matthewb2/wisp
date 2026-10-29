/*
 * Copyright 2011-2016 Vincent Sanders <vince@netsurf-browser.org>
 *
 * This file is part of NetSurf, http://www.netsurf-browser.org/
 *
 * NetSurf is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * NetSurf is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * \file
 * Main browser window handling for windows win32 frontend.
 */

#include "wisp/utils/config.h"

#include <wisp/ns_inttypes.h>
#include <stdbool.h>
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <imm.h>

#include "wisp/browser_window.h"
#include "wisp/content/content.h"
#include "wisp/desktop/browser_history.h"
#include "wisp/keypress.h"
#include "wisp/utils/errors.h"
#include "wisp/utils/log.h"
#include "wisp/utils/messages.h"
#include "wisp/utils/nsoption.h"
#include "wisp/utils/nsurl.h"
#include "wisp/utils/utils.h"
#include "wisp/window.h"

#include "wisp/desktop/searchweb.h"
#include "windows/about.h"
#include "windows/cookies.h"
#include "windows/drawable.h"
#include "windows/findfile.h"
#include "windows/font.h"
#include "windows/global_history.h"
#include "windows/gui.h"
#include "windows/hotlist.h"
#include "windows/local_history.h"
#include "windows/pointers.h"
#include "windows/prefs.h"
#include "windows/resourceid.h"
#include "windows/windbg.h"
#include "windows/window.h"

#include <wisp/content/backing_store.h>
#include <wisp/desktop/gui_internal.h>
#include <wisp/utils/file.h>
#include "content/handlers/image/image_cache.h"
#include "content/llcache.h"

/**
 * List of all gui windows
 */
static struct gui_window *window_list = NULL;

/**
 * Table of win32 windows
 */
struct gui_window *win32_window_table[64] = { NULL };

/**
 * The main window class name
 */
static const LPCWSTR windowclassname_main = L"nswsmainwindow";

/**
 * width of the throbber element
 */
#define NSWS_THROBBER_WIDTH 24

/**
 * height of the url entry box
 */
#define NSWS_URLBAR_HEIGHT 24

/**
 * height of the Page Information bitmap button
 */
#define NSW32_PGIBUTTON_HEIGHT 24

/**
 * Number of open windows
 */
static int open_windows = 0;


/* Function pointers for dynamic Imm32 binding */
typedef HIMC (WINAPI *pfnImmGetContext)(HWND);
typedef BOOL (WINAPI *pfnImmReleaseContext)(HWND, HIMC);
typedef BOOL (WINAPI *pfnImmSetCompositionWindow)(HIMC, LPCOMPOSITIONFORM);
typedef LONG (WINAPI *pfnImmGetCompositionStringW)(HIMC, DWORD, LPVOID, DWORD);

static pfnImmGetContext fnImmGetContext = NULL;
static pfnImmReleaseContext fnImmReleaseContext = NULL;
static pfnImmSetCompositionWindow fnImmSetCompositionWindow = NULL;
static pfnImmGetCompositionStringW fnImmGetCompositionStringW = NULL;
static bool imm_initialized = false;

static void nsws_init_imm(void)
{
    if (imm_initialized) return;
    HMODULE hImm = LoadLibraryA("imm32.dll");
    if (hImm != NULL) {
        fnImmGetContext = (pfnImmGetContext)GetProcAddress(hImm, "ImmGetContext");
        fnImmReleaseContext = (pfnImmReleaseContext)GetProcAddress(hImm, "ImmReleaseContext");
        fnImmSetCompositionWindow = (pfnImmSetCompositionWindow)GetProcAddress(hImm, "ImmSetCompositionWindow");
        fnImmGetCompositionStringW = (pfnImmGetCompositionStringW)GetProcAddress(hImm, "ImmGetCompositionStringW");
    }
    imm_initialized = true;
}

/**
 * IME Composition Start handler
 */
static void nsws_ime_start_composition(HWND hwnd, struct gui_window *gw)
{
    nsws_init_imm();
    if (fnImmGetContext == NULL || fnImmSetCompositionWindow == NULL || fnImmReleaseContext == NULL)
        return;

    HIMC hIMC = fnImmGetContext(hwnd);
    if (hIMC != NULL) {
        COMPOSITIONFORM CompForm;
        CompForm.dwStyle = CFS_POINT;
        GetCaretPos(&CompForm.ptCurrentPos);

        fnImmSetCompositionWindow(hIMC, &CompForm);
        fnImmReleaseContext(hwnd, hIMC);
    }
}

/**
 * IME Composition End handler
 */
static void nsws_ime_end_composition(HWND hwnd, struct gui_window *gw)
{
    /* Clean up any temporary composition state if needed */
}

/**
 * IME Composition handler
 */
static LRESULT nsws_handle_composition(HWND hwnd, struct gui_window *gw, WPARAM wParam, LPARAM lParam)
{
    nsws_init_imm();
    if (lParam & GCS_RESULTSTR) {
        if (fnImmGetContext != NULL && fnImmGetCompositionStringW != NULL &&
            fnImmSetCompositionWindow != NULL && fnImmReleaseContext != NULL) {
            HIMC hIMC = fnImmGetContext(hwnd);
            if (hIMC != NULL) {
                const int maxLen = 256;
                wchar_t wcs[maxLen];
                LONG bytes = fnImmGetCompositionStringW(hIMC, GCS_RESULTSTR, wcs, (maxLen - 1) * sizeof(wchar_t));
                if (bytes > 0) {
                    int wides = bytes / sizeof(wchar_t);
                    wcs[wides] = L'\0';

                    char utf8[maxLen * 3];
                    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wcs, wides, utf8, sizeof(utf8) - 1, NULL, NULL);
                    if (utf8_len > 0) {
                        utf8[utf8_len] = '\0';

                        HWND focus_wnd = GetFocus();
                        if (gw != NULL && focus_wnd == gw->urlbar) {
                            /* Insert result string into URL edit control */
                            SendMessageW(gw->urlbar, EM_REPLACESEL, TRUE, (LPARAM)wcs);
                        } else if (gw != NULL && gw->bw != NULL) {
                            /* Send characters to browser window */
                            for (int i = 0; i < utf8_len; i++) {
                                browser_window_key_press(gw->bw, (uint32_t)(unsigned char)utf8[i]);
                            }
                        }
                    }
                }

                /* Update composition window position */
                COMPOSITIONFORM CompForm;
                CompForm.dwStyle = CFS_POINT;
                GetCaretPos(&CompForm.ptCurrentPos);
                fnImmSetCompositionWindow(hIMC, &CompForm);

                fnImmReleaseContext(hwnd, hIMC);
                return 0;
            }
        }
    }
    return DefWindowProcW(hwnd, WM_IME_COMPOSITION, wParam, lParam);
}


/**
 * create and attach accelerator table to main window
 *
 * \param gw gui window context.
 */
static void nsws_window_set_accels(struct gui_window *gw)
{
    int i, nitems = 12;
    ACCEL accels[12];

    for (i = 0; i < nitems; i++) {
        accels[i].fVirt = FCONTROL | FVIRTKEY;
    }

    accels[0].key = 0x51; /* Q */
    accels[0].cmd = IDM_FILE_QUIT;
    accels[1].key = 0x4E; /* N */
    accels[1].cmd = IDM_FILE_OPEN_WINDOW;
    accels[2].key = VK_LEFT;
    accels[2].cmd = IDM_NAV_BACK;
    accels[3].key = VK_RIGHT;
    accels[3].cmd = IDM_NAV_FORWARD;
    accels[4].key = VK_UP;
    accels[4].cmd = IDM_NAV_HOME;
    accels[5].key = VK_BACK;
    accels[5].cmd = IDM_NAV_STOP;
    accels[6].key = VK_SPACE;
    accels[6].cmd = IDM_NAV_RELOAD;
    accels[7].key = 0x4C; /* L */
    accels[7].cmd = IDM_FILE_OPEN_LOCATION;
    accels[8].key = 0x57; /* W */
    accels[8].cmd = IDM_FILE_CLOSE_WINDOW;
    accels[9].key = 0x41; /* A */
    accels[9].cmd = IDM_EDIT_SELECT_ALL;
    accels[10].key = 0x43; /* C */
    accels[10].cmd = IDM_EDIT_COPY;
    accels[11].key = 0x56; /* V */
    accels[11].cmd = IDM_EDIT_PASTE;

    if (gw != NULL && gw->main != NULL) {
        HACCEL hAccel = CreateAcceleratorTableW(accels, nitems);
        if (hAccel != NULL) {
            DestroyAcceleratorTable(hAccel);
        }
    }
}

/**
 * Get GUI window associated with HWND
 */
struct gui_window *nsws_get_gui_window(HWND hwnd)
{
    struct gui_window *gw;

    for (gw = window_list; gw != NULL; gw = gw->next) {
        if (gw->main == hwnd || gw->drawing == hwnd) {
            return gw;
        }
    }

    for (int i = 0; i < 64; i++) {
        if (win32_window_table[i] != NULL) {
            if (win32_window_table[i]->main == hwnd ||
                win32_window_table[i]->drawing == hwnd) {
                return win32_window_table[i];
            }
        }
    }

    return NULL;
}

/**
 * Get main window HWND for GUI window
 */
HWND gui_window_main_window(struct gui_window *gw)
{
    if (gw == NULL) return NULL;
    return gw->main;
}

/**
 * Set scroll position
 */
void win32_window_set_scroll(struct gui_window *gw, int x, int y)
{
    if (gw == NULL || gw->bw == NULL) return;
    /* Implement or trigger scroll update on browser window */
}

/**
 * Navigate to URL or action
 */
nserror nsws_window_go(struct gui_window *gw, const char *url, bool new_tab)
{
    nsurl *nsa_url;
    nserror err;

    if (url == NULL) return NSERROR_BAD_PARAMETER;

    err = nsurl_create(url, &nsa_url);
    if (err != NSERROR_OK) return err;

    if (gw != NULL && gw->bw != NULL) {
        err = browser_window_navigate(gw->bw, nsa_url, NULL, BW_NAVIGATE_NONE, NULL, NULL, NULL);
    }

    nsurl_unref(nsa_url);
    return err;
}

/**
 * Register main window class
 */
bool nsws_create_main_class(HINSTANCE hInstance)
{
    WNDCLASSEXW wc;

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = hInstance;
    wc.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_MAIN_ICON));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszMenuName = MAKEINTRESOURCEW(IDR_MENU_MAIN);
    wc.lpszClassName = windowclassname_main;

    return (RegisterClassExW(&wc) != 0);
}
