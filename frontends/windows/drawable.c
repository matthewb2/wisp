/*
 * Copyright 2011 Vincent Sanders <vince@simtec.co.uk>
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
 * win32 implementation of drawable window showing browser context
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "wisp/utils/config.h"

#include <windows.h>
#include <windowsx.h>

#include "wisp/browser_window.h"
#include "wisp/keypress.h"
#include "wisp/plotters.h"
#include "wisp/utils/errors.h"
#include "wisp/utils/log.h"

#include "windows/drawable.h"
#include "windows/local_history.h"
#include "windows/plot.h"
#include "windows/windbg.h"
#include "windows/window.h"

static const wchar_t *windowclassname_drawable = L"nswsdrawablewindow";

/**
 * Handle wheel scroll messages.
 */
static LRESULT nsws_drawable_wheel(struct gui_window *gw, HWND hwnd, WPARAM wparam)
{
    int delta = GET_WHEEL_DELTA_WPARAM(wparam);
    int key = LOWORD(wparam);
    UINT lines = 3;

    SystemParametersInfo(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);

    if (key == MK_SHIFT) {
        if (gw->requestscrollx != 0)
            return 0;

        SCROLLINFO si;
        int mem;
        int width, height;

        si.cbSize = sizeof(si);
        si.fMask = SIF_ALL;
        GetScrollInfo(hwnd, SB_HORZ, &si);
        mem = si.nPos;

        int ticks = delta / WHEEL_DELTA;
        int dir = (ticks > 0) ? 1 : -1;
        int absTicks = (ticks < 0) ? -ticks : ticks;
        int step = absTicks * 30;
        si.nPos += dir * step;

        if ((gw->bw != NULL) && (browser_window_get_extents(gw->bw, true, &width, &height) == NSERROR_OK)) {
            si.nPos = min(si.nPos, width - gw->width);
        }
        si.nPos = max(si.nPos, 0);
        SetScrollInfo(hwnd, SB_HORZ, &si, TRUE);
        GetScrollInfo(hwnd, SB_HORZ, &si);
        if (si.nPos != mem) {
            struct rect rect;
            rect.x0 = rect.x1 = gw->scrollx + gw->requestscrollx + si.nPos - mem;
            rect.y0 = rect.y1 = gw->scrolly;
            win32_window_set_scroll(gw, &rect);
        }
    } else {
        if (gw->requestscrolly != 0)
            return 0;

        SCROLLINFO si;
        int mem;
        int width, height;

        si.cbSize = sizeof(si);
        si.fMask = SIF_ALL;
        GetScrollInfo(hwnd, SB_VERT, &si);
        mem = si.nPos;

        int step;
        if (lines == WHEEL_PAGESCROLL) {
            int z = delta / WHEEL_DELTA;
            int absTicks = (z < 0) ? -z : z;
            step = absTicks * gw->height;
            si.nPos += (delta > 0) ? -step : step;
        } else {
            int z = (delta * (int)lines) / WHEEL_DELTA;
            int absLines = (z < 0) ? -z : z;
            step = absLines * 30;
            si.nPos += (delta > 0) ? -step : step;
        }

        if ((gw->bw != NULL) && (browser_window_get_extents(gw->bw, true, &width, &height) == NSERROR_OK)) {
            si.nPos = min(si.nPos, height - gw->height);
        }
        si.nPos = max(si.nPos, 0);
        SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
        GetScrollInfo(hwnd, SB_VERT, &si);
        if (si.nPos != mem) {
            struct rect rect;
            rect.x0 = rect.x1 = gw->scrollx;
            rect.y0 = rect.y1 = gw->scrolly + gw->requestscrolly + si.nPos - mem;
            win32_window_set_scroll(gw, &rect);
        }
    }

    return 0;
}


/**
 * Handle vertical scroll messages.
 */
static LRESULT nsws_drawable_vscroll(struct gui_window *gw, HWND hwnd, WPARAM wparam)
{
    int width, height;
    SCROLLINFO si;
    int mem;

    NSLOG(wisp, INFO, "VSCROLL %d", gw->requestscrolly);

    if (gw->requestscrolly != 0)
        return 0;

    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    GetScrollInfo(hwnd, SB_VERT, &si);
    mem = si.nPos;

    switch (LOWORD(wparam)) {
    case SB_TOP:
        si.nPos = si.nMin;
        break;

    case SB_BOTTOM:
        si.nPos = si.nMax;
        break;

    case SB_LINEUP:
        si.nPos -= 30;
        break;

    case SB_LINEDOWN:
        si.nPos += 30;
        break;

    case SB_PAGEUP:
        si.nPos -= gw->height;
        break;

    case SB_PAGEDOWN:
        si.nPos += gw->height;
        break;

    case SB_THUMBTRACK:
        si.nPos = si.nTrackPos;
        break;

    default:
        break;
    }

    si.fMask = SIF_POS;
    if ((gw->bw != NULL) && (browser_window_get_extents(gw->bw, true, &width, &height) == NSERROR_OK)) {
        si.nPos = min(si.nPos, height - gw->height);
    }

    si.nPos = max(si.nPos, 0);
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
    GetScrollInfo(hwnd, SB_VERT, &si);
    if (si.nPos != mem) {
        struct rect rect;
        rect.x0 = rect.x1 = gw->scrollx;
        rect.y0 = rect.y1 = gw->scrolly + gw->requestscrolly + si.nPos - mem;
        win32_window_set_scroll(gw, &rect);
    }

    return 0;
}


/**
 * Handle horizontal scroll messages.
 */
static LRESULT nsws_drawable_hscroll(struct gui_window *gw, HWND hwnd, WPARAM wparam)
{
    int width, height;
    SCROLLINFO si;
    int mem;

    NSLOG(wisp, INFO, "HSCROLL %d", gw->requestscrollx);

    if (gw->requestscrollx != 0)
        return 0;

    si.cbSize = sizeof(si);
    si.fMask = SIF_ALL;
    GetScrollInfo(hwnd, SB_HORZ, &si);
    mem = si.nPos;

    switch (LOWORD(wparam)) {
    case SB_LINELEFT:
        si.nPos -= 30;
        break;

    case SB_LINERIGHT:
        si.nPos += 30;
        break;

    case SB_PAGELEFT:
        si.nPos -= gw->width;
        break;

    case SB_PAGERIGHT:
        si.nPos += gw->width;
        break;

    case SB_THUMBTRACK:
        si.nPos = si.nTrackPos;
        break;

    default:
        break;
    }

    si.fMask = SIF_POS;

    if ((gw->bw != NULL) && (browser_window_get_extents(gw->bw, true, &width, &height) == NSERROR_OK)) {
        si.nPos = min(si.nPos, width - gw->width);
    }
    si.nPos = max(si.nPos, 0);
    SetScrollInfo(hwnd, SB_HORZ, &si, TRUE);
    GetScrollInfo(hwnd, SB_HORZ, &si);
    if (si.nPos != mem) {
        struct rect rect;
        rect.x0 = rect.x1 = gw->scrollx + gw->requestscrollx + si.nPos - mem;
        rect.y0 = rect.y1 = gw->scrolly;
        win32_window_set_scroll(gw, &rect);
    }

    return 0;
}


/**
 * Handle resize events.
 */
static LRESULT nsws_drawable_resize(struct gui_window *gw)
{
    browser_window_schedule_reformat(gw->bw);
    return 0;
}

/**
 * Handle unicode character messages.
 */
static LRESULT nsws_drawable_unichar(struct gui_window *gw, HWND hwnd, WPARAM wparam)
{
    uint32_t nskey;

    if (wparam == UNICODE_NOCHAR) {
        return 1;
    }

    nskey = wparam;
    browser_window_key_press(gw->bw, nskey);
    return 0;
}

/**
 * Handle character messages.
 *
 * WM_CHAR is generated when WM_KEYDOWN message are passed to
 * TranslateMessage; wParam is UTF-16.  If the codepoint is 4
 * bytes, there are 2 WM_CHAR message, one with the high
 * surrogate and one with the low surrogate.
 */
static LRESULT nsws_drawable_char(struct gui_window *gw, HWND hwnd, WPARAM wparam)
{
    uint32_t nskey;

    nskey = wparam;

    const uint32_t utf16_hi_surrogate_start = 0xD800;
    const uint32_t utf16_lo_surrogate_start = 0xDC00;
    const uint32_t utf16_surrogate_end = 0xDFFF;

    static uint32_t highSurrogate = 0;

    if ((nskey >= utf16_hi_surrogate_start) && (nskey < utf16_lo_surrogate_start)) {
        highSurrogate = nskey;
    } else {
        if ((nskey >= utf16_lo_surrogate_start) && (nskey <= utf16_surrogate_end)) {
            uint32_t lowSurrogate = nskey;
            nskey = (highSurrogate - utf16_hi_surrogate_start) << 10;
            nskey |= (lowSurrogate - utf16_lo_surrogate_start);
            nskey += 0x10000;
        }
        highSurrogate = 0;

        browser_window_key_press(gw->bw, nskey);
    }

    return 0;
}

/**
 * Handle keydown messages.
 */
static LRESULT nsws_drawable_keydown(struct gui_window *gw, HWND hwnd, WPARAM wparam)
{
    uint32_t i;
    bool shift = ((GetKeyState(VK_SHIFT) & 0x8000) == 0x8000);

    switch (wparam) {
    case VK_LEFT:
        i = NS_KEY_LEFT;
        if (shift)
            SendMessage(hwnd, WM_HSCROLL, MAKELONG(SB_LINELEFT, 0), 0);
        break;

    case VK_RIGHT:
        i = NS_KEY_RIGHT;
        if (shift)
            SendMessage(hwnd, WM_HSCROLL, MAKELONG(SB_LINERIGHT, 0), 0);
        break;

    case VK_UP:
        i = NS_KEY_UP;
        if (shift)
            SendMessage(hwnd, WM_VSCROLL, MAKELONG(SB_LINEUP, 0), 0);
        break;

    case VK_DOWN:
        i = NS_KEY_DOWN;
        if (shift)
            SendMessage(hwnd, WM_VSCROLL, MAKELONG(SB_LINEDOWN, 0), 0);
        break;

    case VK_HOME:
        i = NS_KEY_LINE_START;
        if (shift)
            SendMessage(hwnd, WM_HSCROLL, MAKELONG(SB_PAGELEFT, 0), 0);
        break;

    case VK_END:
        i = NS_KEY_LINE_END;
        if (shift)
            SendMessage(hwnd, WM_HSCROLL, MAKELONG(SB_PAGERIGHT, 0), 0);
        break;

    case VK_DELETE:
        i = NS_KEY_DELETE_RIGHT;
        break;

    case VK_NEXT:
        SendMessage(hwnd, WM_VSCROLL, MAKELONG(SB_PAGEDOWN, 0), 0);
        return 1;

    case VK_PRIOR:
        SendMessage(hwnd, WM_VSCROLL, MAKELONG(SB_PAGEUP, 0), 0);
        return 1;

    default:
        return 1;
    }

    browser_window_key_press(gw->bw, i);

    return 0;
}


/**
 * Create the font used to render the in-progress IME composition string.
 *
 * The core sizes the caret to the line box, which its text areas make 1.3
 * times the font size.  Using the caret height directly as the font height
 * would therefore render the composition noticeably larger than the page
 * text, so convert back to the font size here.
 *
 * \param caret_height The height of the caret in pixels, or 0 for a default.
 * \return A newly created font, or NULL on failure.
 */
static HFONT nsws_ime_create_font(int caret_height)
{
    int font_size;

    if (caret_height <= 0) {
        font_size = 16;
    } else {
        /* ceil(caret_height / 1.3) */
        font_size = (caret_height * 10 + 12) / 13;
    }

    if (font_size <= 0) {
        font_size = 1;
    }

    return CreateFontW(-font_size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, HANGEUL_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Malgun Gothic");
}


/**
 * Measurements of the in-progress IME composition string.
 */
struct nsws_ime_metrics {
    SIZE extent; /**< advance width and cell height of the string */
    int ink_ascent; /**< distance from the baseline to the top of the glyph ink */
    int ink_height; /**< height of the glyph ink */
};

/**
 * Measure the composition string with the font currently selected into an HDC.
 *
 * \param hdc The device context with the composition font selected.
 * \param text The composition string.
 * \param length The number of UTF-16 units in text.
 * \param metrics Updated with the measured values.
 */
static void nsws_ime_get_metrics(HDC hdc, const wchar_t *text, int length, struct nsws_ime_metrics *metrics)
{
    TEXTMETRICW tm;

    metrics->extent.cx = 0;
    metrics->extent.cy = 0;
    metrics->ink_ascent = 0;
    metrics->ink_height = 0;

    if ((length <= 0) || (GetTextExtentPoint32W(hdc, text, length, &metrics->extent) == 0)) {
        return;
    }

    if (GetTextMetricsW(hdc, &tm) == 0) {
        metrics->ink_ascent = metrics->extent.cy;
        metrics->ink_height = metrics->extent.cy;
        return;
    }

    /* 
     * GetGlyphOutlineW 대신 폰트의 표준 TextMetric(tmAscent, tmHeight)을 사용하여 
     * 한글 조합 중 메트릭이 널뛰는 현상과 높이 역전(ink_height < ink_ascent) 오류를 방지합니다.
     */
    metrics->ink_ascent = tm.tmAscent;
    metrics->ink_height = tm.tmHeight;
}


/**
 * Measure the drawing area rectangle covered by the composition preview.
 *
 * \param gw The window whose composition preview to measure.
 * \param rect Updated to the rectangle covered by the preview.
 * \param caret_offset Updated with the horizontal distance from the base caret
 *                     position to the trailing edge of the composition string.
 */
static void nsws_ime_measure(struct gui_window *gw, RECT *rect, int *caret_offset)
{
    HDC hdc;
    HFONT font;
    HFONT oldfont;
    struct nsws_ime_metrics metrics;
    int baseline;
    int height;
    int screen_x;
    int screen_y;

    SetRectEmpty(rect);
    *caret_offset = 0;

    if ((gw == NULL) || (gw->drawingarea == NULL)) {
        return;
    }

    /* The caret is stored in document coordinates; convert to drawing area
     * (screen) coordinates by applying the current scroll offset. */
    screen_x = gw->caret_x - gw->scrollx;
    screen_y = gw->caret_y - gw->scrolly;

    height = (gw->caret_height > 0) ? gw->caret_height : 16;

    SetRect(rect, screen_x, screen_y, screen_x + 2, screen_y + height + 2);

    if (gw->ime_composition_len <= 0) {
        return;
    }

    hdc = GetDC(gw->drawingarea);
    if (hdc == NULL) {
        return;
    }

    font = nsws_ime_create_font(height);
    if (font == NULL) {
        ReleaseDC(gw->drawingarea, hdc);
        return;
    }

    oldfont = SelectObject(hdc, font);
    nsws_ime_get_metrics(hdc, gw->ime_composition, gw->ime_composition_len, &metrics);
    SelectObject(hdc, oldfont);
    DeleteObject(font);

    ReleaseDC(gw->drawingarea, hdc);

    if (metrics.extent.cx <= 0) {
        return;
    }

    /* The core draws the text of a line box on the baseline at three quarters
     * of the line height (see textarea_setup_text_offsets), so do the same. */
    baseline = screen_y + (height * 3 + 2) / 4;

    /* The preview rectangle includes the two pixel underline strip so that it
     * is erased together with the text when the composition ends. */
    rect->top = baseline - metrics.ink_ascent;
    rect->right = screen_x + metrics.extent.cx + 3;
    rect->bottom = rect->top + metrics.ink_height + 2;

    /* The text is drawn one pixel to the right of the base caret position. */
    *caret_offset = metrics.extent.cx + 1;
}


/**
 * Invalidate the area occupied by the IME composition preview.
 *
 * Called when the in-progress composition string changes or ends so that the
 * page and the preview are redrawn without leaving stale text behind.
 *
 * \param gw The window whose composition preview needs repainting.
 */
void nsws_drawable_ime_invalidate(struct gui_window *gw)
{
    RECT oldrect;
    RECT newrect;
    RECT unionrect;
    int caret_offset;

    if ((gw == NULL) || (gw->drawingarea == NULL)) {
        return;
    }

    oldrect = gw->ime_rect;
    nsws_ime_measure(gw, &newrect, &caret_offset);

    if (!IsRectEmpty(&oldrect)) {
        UnionRect(&unionrect, &oldrect, &newrect);
        newrect = unionrect;
    }

    InvalidateRect(gw->drawingarea, &newrect, FALSE);

    if (gw->caret_height <= 0) {
        /* No caret has been placed in the drawing area yet. */
        return;
    }

    /* While a composition is in progress the caret belongs after the
     * composition string rather than at the point where it started.  When the
     * composition ends caret_offset is zero, which puts the caret back at the
     * position provided by the core. */
    HideCaret(gw->drawingarea);
    SetCaretPos(gw->caret_x - gw->scrollx + caret_offset, gw->caret_y - gw->scrolly);
    ShowCaret(gw->drawingarea);
}


/**
 * Draw the in-progress IME composition string at the caret.
 *
 * The text is drawn by the frontend (the IME's own composition window is
 * suppressed) so that the composition is visible directly in the page with an
 * underline, matching the behaviour of native Windows text controls.
 *
 * \param gw The window whose composition preview to draw.
 * \param hdc The device context to draw on.
 */
static void nsws_ime_paint(struct gui_window *gw, HDC hdc)
{
    HFONT font;
    HFONT oldfont;
    RECT rect;
    RECT underline;
    struct nsws_ime_metrics metrics;
    COLORREF oldcolour;
    UINT oldalign;
    int oldmode;
    int baseline;
    int height;
    int screen_x;
    int screen_y;
    int padding_right;
    int underline_thickness;    

    SetRectEmpty(&gw->ime_rect);

    if ((gw == NULL) || (gw->ime_composition_len <= 0) || (hdc == NULL)) {
        return;
    }

    /* The caret is stored in document coordinates; convert to drawing area
     * (screen) coordinates by applying the current scroll offset. */
    screen_x = gw->caret_x - gw->scrollx;
    screen_y = gw->caret_y - gw->scrolly;

    height = (gw->caret_height > 0) ? gw->caret_height : 16;
    
    /* 폰트 크기에 비례하여 여백과 밑줄 두께를 동적으로 계산합니다. */
    padding_right = max(2, height / 6);          // 폰트 크기의 약 1/6을 가로 여백으로 부여
    underline_thickness = max(1, height / 12);    // 폰트 크기에 비례하되 최소 1~2픽셀 유지

    font = nsws_ime_create_font(height);
    if (font == NULL) {
        return;
    }

    oldfont = SelectObject(hdc, font);

    nsws_ime_get_metrics(hdc, gw->ime_composition, gw->ime_composition_len, &metrics);
    if (metrics.extent.cx <= 0) {
        SelectObject(hdc, oldfont);
        DeleteObject(font);
        return;
    }

    /* The core draws the text of a line box on the baseline at three quarters
     * of the line height (see textarea_setup_text_offsets), so do the same. */
    baseline = screen_y + (height * 3 + 2) / 4;
    
    /* 디버깅 정보 출력 (라벨과 전달 인자 매칭 수정) */
    fprintf(stderr, "[IME_DEBUG] len:%d | caret:(%d, %d) scroll:(%d, %d) | screen:(%d, %d)\n"
                    "            | height:%d | extent:(cx:%d, cy:%d) | ink_ascent:%d, ink_height:%d | baseline:%d\n",
            gw->ime_composition_len,
            gw->caret_x, gw->caret_y,
            gw->scrollx, gw->scrolly,
            screen_x, screen_y,
            height,
            metrics.extent.cx, metrics.extent.cy,
            metrics.ink_ascent, metrics.ink_height, 
            baseline);

    /* The preview rectangle covers the caret box area cleanly using dynamic padding */
    SetRect(&rect, screen_x, screen_y, screen_x + metrics.extent.cx + padding_right, screen_y + height);
    gw->ime_rect = rect;

    /* Opaque background so the composition stays legible over page content. */
    FillRect(hdc, &rect, (HBRUSH)GetStockObject(WHITE_BRUSH));

    /* The plotters leave the device context with TA_BASELINE set, but the
     * alignment is set explicitly here so the text follows the baseline that
     * was calculated above. */
    oldalign = SetTextAlign(hdc, TA_BASELINE | TA_LEFT);
    oldcolour = SetTextColor(hdc, RGB(0, 0, 0));
    oldmode = SetBkMode(hdc, TRANSPARENT);

    ExtTextOutW(
        hdc, screen_x + 1, baseline, ETO_CLIPPED, &rect, gw->ime_composition, (UINT)gw->ime_composition_len, NULL);

    SetBkMode(hdc, oldmode);
    SetTextColor(hdc, oldcolour);
    SetTextAlign(hdc, oldalign);

    /* 밑줄: 캐럿 하단 내부에서 동적 두께(underline_thickness)만큼 차지하도록 설정 */
    SetRect(&underline, screen_x, screen_y + height - underline_thickness,
        screen_x + metrics.extent.cx + padding_right, screen_y + height);
    FillRect(hdc, &underline, (HBRUSH)GetStockObject(BLACK_BRUSH));

    SelectObject(hdc, oldfont);
    DeleteObject(font);
}

/**
 * Handle paint messages.
 */
static LRESULT nsws_drawable_paint(struct gui_window *gw, HWND hwnd)
{
    struct rect clip;
    PAINTSTRUCT ps;
    struct redraw_context ctx = {.interactive = true, .background_images = true, .plot = &win_plotters};

    BeginPaint(hwnd, &ps);

    if (gw != NULL) {
        plot_hdc = ps.hdc;
        plot_gw = gw;

        clip.x0 = ps.rcPaint.left;
        clip.y0 = ps.rcPaint.top;
        clip.x1 = ps.rcPaint.right;
        clip.y1 = ps.rcPaint.bottom;

        /**
         * \todo work out why the heck scroll needs scaling
         */

        browser_window_redraw(gw->bw, -gw->scrollx, -gw->scrolly, &clip, &ctx);

        nsws_ime_paint(gw, ps.hdc);

        plot_gw = NULL;
    }

    EndPaint(hwnd, &ps);

    return 0;
}


/**
 * Handle mouse button up messages.
 */
static LRESULT
nsws_drawable_mouseup(struct gui_window *gw, int x, int y, browser_mouse_state press, browser_mouse_state click)
{
    bool shift = ((GetKeyState(VK_SHIFT) & 0x8000) == 0x8000);
    bool ctrl = ((GetKeyState(VK_CONTROL) & 0x8000) == 0x8000);
    bool alt = ((GetKeyState(VK_MENU) & 0x8000) == 0x8000);

    if ((gw == NULL) || (gw->mouse == NULL) || (gw->bw == NULL))
        return 0;

    NSLOG(wisp, INFO, "state 0x%x, press 0x%x", gw->mouse->state, press);
    if ((gw->mouse->state & press) != 0) {
        gw->mouse->state &= ~press;
        gw->mouse->state |= click;
    }

    if (((gw->mouse->state & BROWSER_MOUSE_MOD_1) != 0) && !shift)
        gw->mouse->state &= ~BROWSER_MOUSE_MOD_1;
    if (((gw->mouse->state & BROWSER_MOUSE_MOD_2) != 0) && !ctrl)
        gw->mouse->state &= ~BROWSER_MOUSE_MOD_2;
    if (((gw->mouse->state & BROWSER_MOUSE_MOD_3) != 0) && !alt)
        gw->mouse->state &= ~BROWSER_MOUSE_MOD_3;

    if ((gw->mouse->state & click) != 0) {
        NSLOG(wisp, INFO, "mouse click bw %p, state 0x%x, x %d, y %d", gw->bw, gw->mouse->state, x + gw->scrollx,
            y + gw->scrolly);

        browser_window_mouse_click(gw->bw, gw->mouse->state, x + gw->scrollx, y + gw->scrolly);
    } else {
        browser_window_mouse_track(gw->bw, 0, x + gw->scrollx, y + gw->scrolly);
    }

    gw->mouse->state = 0;
    return 0;
}


/**
 * Handle mouse button down messages.
 */
static LRESULT nsws_drawable_mousedown(struct gui_window *gw, int x, int y, browser_mouse_state button)
{
    if ((gw == NULL) || (gw->mouse == NULL) || (gw->bw == NULL)) {
        nsw32_local_history_hide();
        return 0;
    }

    gw->mouse->state = button;
    if ((GetKeyState(VK_SHIFT) & 0x8000) == 0x8000)
        gw->mouse->state |= BROWSER_MOUSE_MOD_1;
    if ((GetKeyState(VK_CONTROL) & 0x8000) == 0x8000)
        gw->mouse->state |= BROWSER_MOUSE_MOD_2;
    if ((GetKeyState(VK_MENU) & 0x8000) == 0x8000)
        gw->mouse->state |= BROWSER_MOUSE_MOD_3;

    gw->mouse->pressed_x = x + gw->scrollx;
    gw->mouse->pressed_y = y + gw->scrolly;

    NSLOG(wisp, INFO, "mouse click bw %p, state %x, x %d, y %d", gw->bw, gw->mouse->state, x + gw->scrollx,
        y + gw->scrolly);

    browser_window_mouse_click(gw->bw, gw->mouse->state, x + gw->scrollx, y + gw->scrolly);

    return 0;
}


/**
 * Handle mouse movement messages.
 */
static LRESULT nsws_drawable_mousemove(struct gui_window *gw, int x, int y)
{
    bool shift = ((GetKeyState(VK_SHIFT) & 0x8000) == 0x8000);
    bool ctrl = ((GetKeyState(VK_CONTROL) & 0x8000) == 0x8000);
    bool alt = ((GetKeyState(VK_MENU) & 0x8000) == 0x8000);

    if ((gw == NULL) || (gw->mouse == NULL) || (gw->bw == NULL))
        return 0;

    /* add scroll offsets */
    x = x + gw->scrollx;
    y = y + gw->scrolly;

    /* if mouse button held down and pointer moved more than
     * minimum distance drag is happening */
    if (((gw->mouse->state & (BROWSER_MOUSE_PRESS_1 | BROWSER_MOUSE_PRESS_2)) != 0) &&
        (fabs(x - gw->mouse->pressed_x) >= 5) && (fabs(y - gw->mouse->pressed_y) >= 5)) {

        NSLOG(wisp, INFO, "Drag start state 0x%x", gw->mouse->state);

        if ((gw->mouse->state & BROWSER_MOUSE_PRESS_1) != 0) {
            browser_window_mouse_click(gw->bw, BROWSER_MOUSE_DRAG_1, gw->mouse->pressed_x, gw->mouse->pressed_y);
            gw->mouse->state &= ~BROWSER_MOUSE_PRESS_1;
            gw->mouse->state |= BROWSER_MOUSE_HOLDING_1 | BROWSER_MOUSE_DRAG_ON;
        } else if ((gw->mouse->state & BROWSER_MOUSE_PRESS_2) != 0) {
            browser_window_mouse_click(gw->bw, BROWSER_MOUSE_DRAG_2, gw->mouse->pressed_x, gw->mouse->pressed_y);
            gw->mouse->state &= ~BROWSER_MOUSE_PRESS_2;
            gw->mouse->state |= BROWSER_MOUSE_HOLDING_2 | BROWSER_MOUSE_DRAG_ON;
        }
    }

    if (((gw->mouse->state & BROWSER_MOUSE_MOD_1) != 0) && !shift)
        gw->mouse->state &= ~BROWSER_MOUSE_MOD_1;
    if (((gw->mouse->state & BROWSER_MOUSE_MOD_2) != 0) && !ctrl)
        gw->mouse->state &= ~BROWSER_MOUSE_MOD_2;
    if (((gw->mouse->state & BROWSER_MOUSE_MOD_3) != 0) && !alt)
        gw->mouse->state &= ~BROWSER_MOUSE_MOD_3;


    browser_window_mouse_track(gw->bw, gw->mouse->state, x, y);

    return 0;
}


/**
 * Called when activity occours within the drawable window.
 */
static LRESULT CALLBACK nsws_window_drawable_event_callback(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    struct gui_window *gw;

    LOG_WIN_MSG(hwnd, msg, wparam, lparam);

    gw = nsws_get_gui_window(hwnd);
    if (gw == NULL) {
        NSLOG(wisp, INFO, "Unable to find gui window structure for hwnd %p", hwnd);
        return DefWindowProc(hwnd, msg, wparam, lparam);
    }

    switch (msg) {

    case WM_MOUSEMOVE:
        return nsws_drawable_mousemove(gw, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));

    case WM_LBUTTONDOWN:
        nsws_drawable_mousedown(gw, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), BROWSER_MOUSE_PRESS_1);
        SetFocus(hwnd);
        nsw32_local_history_hide();
        return 0;

    case WM_RBUTTONDOWN:
        nsws_drawable_mousedown(gw, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), BROWSER_MOUSE_PRESS_2);
        SetFocus(hwnd);
        return 0;

    case WM_LBUTTONUP:
        return nsws_drawable_mouseup(
            gw, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), BROWSER_MOUSE_PRESS_1, BROWSER_MOUSE_CLICK_1);

    case WM_RBUTTONUP:
        return nsws_drawable_mouseup(
            gw, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam), BROWSER_MOUSE_PRESS_2, BROWSER_MOUSE_CLICK_2);

    case WM_ERASEBKGND: /* ignore as drawable window is redrawn on paint */
        return 0;

    case WM_PAINT: /* redraw the exposed part of the window */
        return nsws_drawable_paint(gw, hwnd);

    case WM_KEYDOWN:
        if (nsws_drawable_keydown(gw, hwnd, wparam) == 0) {
            return 0;
        }
        break;

    case WM_CHAR:
        return nsws_drawable_char(gw, hwnd, wparam);

    case WM_UNICHAR:
        return nsws_drawable_unichar(gw, hwnd, wparam);

    case WM_IME_STARTCOMPOSITION:
        nsws_ime_start_composition(hwnd, gw);
        return 0;

    case WM_IME_SETCONTEXT:
        /* The composition string is drawn by nsws_ime_paint, so ask the IME
         * not to draw its own floating composition window as well.  The
         * candidate window is unaffected. */
        lparam &= ~ISC_SHOWUICOMPOSITIONWINDOW;
        return DefWindowProc(hwnd, msg, wparam, lparam);

    case WM_IME_ENDCOMPOSITION:
        nsws_ime_end_composition(hwnd, gw);
        return 0;

    case WM_IME_COMPOSITION:
        return nsws_handle_composition(hwnd, gw, wparam, lparam);

    case WM_SIZE:
        return nsws_drawable_resize(gw);

    case WM_HSCROLL:
        return nsws_drawable_hscroll(gw, hwnd, wparam);

    case WM_VSCROLL:
        return nsws_drawable_vscroll(gw, hwnd, wparam);

    case WM_MOUSEWHEEL:
        return nsws_drawable_wheel(gw, hwnd, wparam);

    case WM_PASTE:
        browser_window_key_press(gw->bw, NS_KEY_PASTE);
        return 0;

    case WM_COPY:
        browser_window_key_press(gw->bw, NS_KEY_COPY_SELECTION);
        return 0;

    case WM_CUT:
        browser_window_key_press(gw->bw, NS_KEY_CUT_SELECTION);
        return 0;

    case WM_CLEAR:
        /**
         * \todo win32 clear operation deletes the contents of
         *       the selection but ns clear selection only
         *       removes the highlight.
         */
        browser_window_key_press(gw->bw, NS_KEY_CLEAR_SELECTION);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wparam, lparam);
}


/**
 * Create a drawable window.
 */
HWND nsws_window_create_drawable(HINSTANCE hinstance, HWND hparent, struct gui_window *gw)
{
    HWND hwnd;
    hwnd = CreateWindowExW(
        0, windowclassname_drawable, NULL, WS_VISIBLE | WS_CHILD, 0, 0, 0, 0, hparent, NULL, hinstance, NULL);

    if (hwnd == NULL) {
        win_perror("WindowCreateDrawable");
        NSLOG(wisp, INFO, "Window creation failed");
        return NULL;
    }

    /* set the gui window associated with this toolbar */
    SetProp(hwnd, TEXT("GuiWnd"), (HANDLE)gw);

    return hwnd;
}


/**
 * Create the drawable window class.
 */
nserror nsws_create_drawable_class(HINSTANCE hinstance)
{
    nserror ret = NSERROR_OK;
    WNDCLASSEXW w;

    /* drawable area */
    w.cbSize = sizeof(WNDCLASSEX);
    w.style = 0;
    w.lpfnWndProc = nsws_window_drawable_event_callback;
    w.cbClsExtra = 0;
    w.cbWndExtra = 0;
    w.hInstance = hinstance;
    w.hIcon = NULL;
    w.hCursor = NULL;
    w.hbrBackground = (HBRUSH)(COLOR_MENU + 1);
    w.lpszMenuName = NULL;
    w.lpszClassName = windowclassname_drawable;
    w.hIconSm = NULL;

    if (RegisterClassExW(&w) == 0) {
        win_perror("DrawableClass");
        ret = NSERROR_INIT_FAILED;
    }

    return ret;
}
