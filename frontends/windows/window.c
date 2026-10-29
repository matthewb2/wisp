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


/**
 * IME Composition Start handler
 */
static void nsws_ime_start_composition(HWND hwnd, struct gui_window *gw)
{
    HIMC hIMC = ImmGetContext(hwnd);
    if (hIMC != NULL) {
        COMPOSITIONFORM CompForm;
        CompForm.dwStyle = CFS_POINT;
        GetCaretPos(&CompForm.ptCurrentPos);

        ImmSetCompositionWindow(hIMC, &CompForm);
        ImmReleaseContext(hwnd, hIMC);
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
    if (lParam & GCS_RESULTSTR) {
        HIMC hIMC = ImmGetContext(hwnd);
        if (hIMC != NULL) {
            const int maxLen = 256;
            wchar_t wcs[maxLen];
            LONG bytes = ImmGetCompositionStringW(hIMC, GCS_RESULTSTR, wcs, (maxLen - 1) * sizeof(wchar_t));
            if (bytes > 0) {
                int wides = bytes / sizeof(wchar_t);
                wcs[wides] = L'\0';

                char utf8[maxLen * 3];
                int len = MultiByteToWideChar(CP_UTF8, 0, (char *)wcs, -1, NULL, 0);
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
            ImmSetCompositionWindow(hIMC, &CompForm);

            ImmReleaseContext(hwnd, hIMC);
            return 0;
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
    int i, nitems = 13;
    ACCEL accels[nitems];

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
    accels[8].key = 0x57; /* w */
    accels[8].cmd = IDM_FILE_CLOSE_WINDOW;
    accels[9].key = 0x41; /* A */
    accels[9].cmd = IDM_EDIT_SELECT_ALL;
    accels[10].key = VK_F8;
    accels[10].cmd = IDM_VIEW_SOURCE;
    accels[11].key = VK_RETURN;
    accels[11].fVirt = FVIRTKEY;
    accels[11].cmd = IDC_MAIN_LAUNCH_URL;
    accels[12].key = VK_F11;
    accels[12].fVirt = FVIRTKEY;
    accels[12].cmd = IDM_VIEW_FULLSCREEN;

    gw->acceltable = CreateAcceleratorTable(accels, nitems);
}


/**
 * creation of a new full browser window
 *
 * \param hInstance The application instance handle.
 * \param gw gui window context.
 * \return The newly created window instance.
 */
static HWND nsws_window_create(HINSTANCE hInstance, struct gui_window *gw)
{
    HWND hwnd;
    INITCOMMONCONTROLSEX icc;
    int xpos = CW_USEDEFAULT;
    int ypos = CW_USEDEFAULT;
    int width = CW_USEDEFAULT;
    int height = CW_USEDEFAULT;

    if ((nsoption_int(window_width) >= 100) && (nsoption_int(window_height) >= 100) && (nsoption_int(window_x) >= 0) &&
        (nsoption_int(window_y) >= 0)) {
        xpos = nsoption_int(window_x);
        ypos = nsoption_int(window_y);
        width = nsoption_int(window_width);
        height = nsoption_int(window_height);

        NSLOG(wisp, DEBUG, "Setting Window position %d,%d %d,%d", xpos, ypos, width, height);
    }

    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES | ICC_WIN95_CLASSES;
#if WINVER > 0x0501
    icc.dwICC |= ICC_STANDARD_CLASSES;
#endif
    InitCommonControlsEx(&icc);

    gw->mainmenu = LoadMenu(hInstance, MAKEINTRESOURCE(IDR_MENU_MAIN));
    gw->rclick = LoadMenu(hInstance, MAKEINTRESOURCE(IDR_MENU_CONTEXT));

    hwnd = CreateWindowExW(0, windowclassname_main, L"Wisp Browser",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN | WS_CLIPSIBLINGS | CS_DBLCLKS, xpos, ypos, width, height, NULL,
        gw->mainmenu, hInstance, (LPVOID)gw);

    if (hwnd == NULL) {
        NSLOG(wisp, INFO, "Window create failed");
    } else {
        nsws_window_set_accels(gw);
    }

    return hwnd;
}


/**
 * toolbar command message handler
 *
 * \todo This entire command handler appears superfluous.
 *
 * \param gw The graphical window context
 * \param notification_code The notification code of the message
 * \param identifier The identifier the command was delivered for
 * \param ctrl_window The controlling window.
 */
static LRESULT
nsws_window_toolbar_command(struct gui_window *gw, int notification_code, int identifier, HWND ctrl_window)
{
    NSLOG(wisp, DEBUG, "notification_code %d identifier %d ctrl_window %p", notification_code, identifier,
        ctrl_window);

    switch (identifier) {

    case IDC_MAIN_URLBAR:
        switch (notification_code) {
        case EN_CHANGE:
            NSLOG(wisp, DEBUG, "EN_CHANGE");
            break;

        case EN_ERRSPACE:
            NSLOG(wisp, DEBUG, "EN_ERRSPACE");
            break;

        case EN_HSCROLL:
            NSLOG(wisp, DEBUG, "EN_HSCROLL");
            break;

        case EN_KILLFOCUS:
            NSLOG(wisp, DEBUG, "EN_KILLFOCUS");
            break;

        case EN_MAXTEXT:
            NSLOG(wisp, DEBUG, "EN_MAXTEXT");
            break;

        case EN_SETFOCUS:
            NSLOG(wisp, DEBUG, "EN_SETFOCUS");
            break;

        case EN_UPDATE:
            NSLOG(wisp, DEBUG, "EN_UPDATE");
            break;

        case EN_VSCROLL:
            NSLOG(wisp, DEBUG, "EN_VSCROLL");
            break;

        default:
            NSLOG(wisp, DEBUG, "Unknown notification_code");
            break;
        }
        break;

    default:
        return 1; /* unhandled */
    }
    return 0; /* control message handled */
}


/**
 * calculate the dimensions of the url bar relative to the parent toolbar
 *
 * \param hWndParent The parent window of the url bar
 * \param toolbuttonsize size of the buttons
 * \param buttonc The number of buttons
 * \param[out] x The calculated x location
 * \param[out] y The calculated y location
 * \param[out] width The calculated width
 * \param[out] height The calculated height
 */
static void urlbar_dimensions(HWND hWndParent, int toolbuttonsize, int buttonc, int *x, int *y, int *width, int *height)
{
    RECT rc;
    const int cy_edit = NSWS_URLBAR_HEIGHT;

    GetClientRect(hWndParent, &rc);
    *x = (toolbuttonsize + 1) * (buttonc + 1) + (NSWS_THROBBER_WIDTH >> 1);
    *y = ((((rc.bottom - 1) - cy_edit) >> 1) * 2) / 3;
    *width = (rc.right - 1) - *x - (NSWS_THROBBER_WIDTH >> 1) - NSWS_THROBBER_WIDTH;
    *height = cy_edit;
}


/**
 * callback for toolbar events
 *
 * subclass message handler for toolbar window
 *
 * \param hwnd win32 window handle message arrived for
 * \param msg The message ID
 * \param wparam The w parameter of the message.
 * \param lparam The l parameter of the message.
 */
static LRESULT CALLBACK nsws_window_toolbar_callback(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    struct gui_window *gw;
    int urlx, urly, urlwidth, urlheight;
    WNDPROC toolproc;

    LOG_WIN_MSG(hwnd, msg, wparam, lparam);

    toolproc = (WNDPROC)GetProp(hwnd, TEXT("OrigMsgProc"));
    assert(toolproc != NULL);

    gw = nsws_get_gui_window(hwnd);
    assert(gw != NULL);

    switch (msg) {
    case WM_SIZE:
        urlbar_dimensions(hwnd, gw->toolbuttonsize, gw->toolbuttonc, &urlx, &urly, &urlwidth, &urlheight);

        /* resize url */
        if (gw->urlbar != NULL) {
            MoveWindow(gw->urlbar, urlx,