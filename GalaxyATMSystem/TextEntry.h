#pragma once

#include <windows.h>
#include <functional>
#include <string>

// -----------------------------------------------------------------------------
// A line of text typed straight into a Windows edit box laid over a field the
// plug-in draws itself - for the Вход в систему КСА window, where EuroScope's
// own popup edit will not do: it shows a password as it is typed, and passes
// Cyrillic through the ANSI code page, which turns it into "?" on a Windows
// without a Russian one.
//
// The box is a top-level popup owned by EuroScope's window rather than a child
// of the radar view: EuroScope paints the view from its own buffer and would
// paint over a child. It ends with Enter, Tab or Esc, or when its owner closes
// it - never by losing the focus, since EuroScope may take the focus back
// straight after the click that opened it, and a box that shut itself then
// could never be typed into.
// -----------------------------------------------------------------------------
class TextEntry
{
public:
    enum class End { Submit, Next, Cancel };   // Enter / Tab / Esc

    TextEntry() = default;
    TextEntry(const TextEntry&) = delete;
    TextEntry& operator=(const TextEntry&) = delete;
    ~TextEntry() { Close(); }

    // Opens the box over 'field', given in 'view''s client coordinates, holding
    // 'text' with the caret at its end; a box already open is closed first.
    // 'onEnd' is called once the key that ends it has been pressed, with the
    // box still open so Text() still reads it - closing it is the callback's
    // job. False when the box could not be made.
    bool Open(HWND view, const RECT& field, HFONT font, const std::wstring& text, bool password,
        int maxChars, std::function<void(End)> onEnd);

    // Keeps the box over its field when the window it belongs to is dragged.
    void Move(const RECT& field);

    std::wstring Text() const;
    bool IsOpen() const { return m_edit != NULL; }
    void Close();

private:
    static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, UINT_PTR id, DWORD_PTR data);
    RECT ScreenRect(const RECT& field) const;

    HWND m_view = NULL;
    HWND m_edit = NULL;
    RECT m_placed = { 0, 0, 0, 0 };
    std::function<void(End)> m_onEnd;
};
