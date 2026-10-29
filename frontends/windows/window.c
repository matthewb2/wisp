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
#include "wisp/types.h"
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
struct gui_window_table *win32_window_table = NULL;

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
                if (bytes >