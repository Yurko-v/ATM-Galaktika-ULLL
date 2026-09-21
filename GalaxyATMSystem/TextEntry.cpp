#include "pch.h"
#include "TextEntry.h"
#include "Log.h"

#include <commctrl.h>
#pragma comment(lib, "comctl32.lib")

namespace
{
    const UINT_PTR kSubclassId = 1;

    const UINT kEndMessage = WM_APP + 0x47;
}

RECT TextEntry::ScreenRect(const RECT& field) const
{
    POINT tl = { field.left, field.top };
    POINT br = { field.right, field.bottom };
    ClientToScreen(m_view, &tl);
    ClientToScreen(m_view, &br);
    RECT r = { tl.x, tl.y, br.x, br.y };
    return r;
}

bool TextEntry::Open(HWND view, const RECT& field, HFONT font, const std::wstring& text, bool password,
    int maxChars, std::function<void(End)> onEnd)
{
    Close();
    if (view == NULL || !IsWindow(view))
        return false;

    m_view = view;
    const RECT r = ScreenRect(field);
    DWORD style = WS_POPUP | WS_BORDER | ES_LEFT | ES_AUTOHSCROLL;
    if (password)
        style |= ES_PASSWORD;

    HWND edit = CreateWindowExW(WS_EX_TOOLWINDOW, L"EDIT", text.c_str(), style,
        r.left, r.top, r.right - r.left, r.bottom - r.top,
        GetAncestor(view, GA_ROOT), NULL, GetModuleHandleW(NULL), NULL);
    if (edit == NULL)
    {
        Log::Error("entry", "the edit box could not be made - " + Log::SystemError(GetLastError()));
        m_view = NULL;
        return false;
    }
    if (!SetWindowSubclass(edit, Proc, kSubclassId, (DWORD_PTR)this))
    {
        Log::Error("entry", "the edit box could not be subclassed - " + Log::SystemError(GetLastError()));
        DestroyWindow(edit);
        m_view = NULL;
        return false;
    }

    m_edit = edit;
    m_placed = r;
    m_onEnd = std::move(onEnd);

    SendMessageW(edit, WM_SETFONT, (WPARAM)font, FALSE);
    SendMessageW(edit, EM_SETLIMITTEXT, (WPARAM)maxChars, 0);
    SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(5, 5));
    if (password)
        SendMessageW(edit, EM_SETPASSWORDCHAR, (WPARAM)0x25CF, 0);
    const int length = GetWindowTextLengthW(edit);
    SendMessageW(edit, EM_SETSEL, (WPARAM)length, (LPARAM)length);

    ShowWindow(edit, SW_SHOW);
    SetFocus(edit);
    return true;
}

void TextEntry::Move(const RECT& field)
{
    if (m_edit == NULL)
        return;
    const RECT r = ScreenRect(field);
    if (EqualRect(&r, &m_placed))
        return;
    m_placed = r;
    SetWindowPos(m_edit, NULL, r.left, r.top, r.right - r.left, r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

std::wstring TextEntry::Text() const
{
    if (m_edit == NULL)
        return std::wstring();
    const int length = GetWindowTextLengthW(m_edit);
    std::wstring text((size_t)length + 1, L'\0');
    const int got = GetWindowTextW(m_edit, &text[0], length + 1);
    text.resize((size_t)(got > 0 ? got : 0));
    return text;
}

void TextEntry::Close()
{
    HWND edit = m_edit;
    m_edit = NULL;
    m_view = NULL;
    if (edit == NULL)
        return;

    RemoveWindowSubclass(edit, Proc, kSubclassId);
    SetWindowTextW(edit, L"");
    DestroyWindow(edit);
}

LRESULT CALLBACK TextEntry::Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR, DWORD_PTR data)
{
    TextEntry* self = (TextEntry*)data;
    switch (msg)
    {
    case WM_KEYDOWN:
        if (wp == VK_RETURN || wp == VK_TAB || wp == VK_ESCAPE)
        {
            PostMessageW(hwnd, kEndMessage, wp == VK_RETURN ? 0 : (wp == VK_TAB ? 1 : 2), 0);
            return 0;
        }
        break;

    case WM_CHAR:
        if (wp == L'\r' || wp == L'\t' || wp == 0x1B)
            return 0;
        break;

    case kEndMessage:
        if (self->m_edit == hwnd && self->m_onEnd)
        {
            std::function<void(End)> onEnd = self->m_onEnd;
            onEnd(wp == 0 ? End::Submit : (wp == 1 ? End::Next : End::Cancel));
        }
        return 0;

    case WM_NCDESTROY:
        RemoveWindowSubclass(hwnd, Proc, kSubclassId);
        if (self->m_edit == hwnd)
        {
            self->m_edit = NULL;
            self->m_view = NULL;
        }
        break;
    }
    return DefSubclassProc(hwnd, msg, wp, lp);
}
