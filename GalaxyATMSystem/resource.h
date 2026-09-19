#pragma once

// Resources carried inside the DLL.
//
// IDR_FONT_EUROSCOPE is EuroScope.ttf itself, linked into the plugin rather
// than read from disk: the tag labels are drawn in it, and a controller who
// never installed the face still sees the same picture.
#define IDR_FONT_EUROSCOPE  101
