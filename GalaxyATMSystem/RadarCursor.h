#pragma once

#include <windows.h>

namespace RadarCursor
{
    void Attach(HWND view, const RECT& area);
    void DetachAll();
}
