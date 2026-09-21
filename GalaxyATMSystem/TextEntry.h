#pragma once

#include <windows.h>
#include <functional>
#include <string>

class TextEntry
{
public:
    enum class End { Submit, Next, Cancel };

    TextEntry() = default;
    TextEntry(const TextEntry&) = delete;
    TextEntry& operator=(const TextEntry&) = delete;
    ~TextEntry() { Close(); }

    bool Open(HWND view, const RECT& field, HFONT font, const std::wstring& text, bool password,
        int maxChars, std::function<void(End)> onEnd);

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
