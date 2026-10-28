#pragma once
#include <imgui.h>

namespace stltvj
{
// The look: dark surfaces in a few steps of depth (the deepest behind everything, panels above, controls above
// those), one teal accent for whatever is live or selected, and text that keeps at least 4.5:1 contrast.
namespace theme
{
// Surfaces, deepest first.
constexpr ImU32 Base        = IM_COL32( 11, 12, 15, 255 );   // behind the panels
constexpr ImU32 Panel       = IM_COL32( 20, 22, 26, 255 );   // panel background
constexpr ImU32 Well        = IM_COL32( 15, 17, 20, 255 );   // sunken areas: the clip grid, canvases
constexpr ImU32 Raised      = IM_COL32( 30, 33, 39, 255 );   // controls
constexpr ImU32 RaisedHover = IM_COL32( 39, 43, 50, 255 );
constexpr ImU32 Line        = IM_COL32( 255, 255, 255, 18 ); // hairlines and borders

constexpr ImU32 Accent      = IM_COL32( 102, 225, 192, 255 );
constexpr ImU32 AccentDim   = IM_COL32( 40, 110, 94, 255 );
constexpr ImU32 AccentFill  = IM_COL32( 102, 225, 192, 64 ); // slider fills, selections
constexpr ImU32 OnAccent    = IM_COL32( 8, 20, 17, 255 );    // text and icons on the accent
constexpr ImU32 Selected    = IM_COL32( 230, 233, 238, 255 );
constexpr ImU32 CellEmpty   = IM_COL32( 22, 24, 29, 255 );
constexpr ImU32 CellBorder  = IM_COL32( 255, 255, 255, 14 );
constexpr ImU32 Text        = IM_COL32( 230, 232, 236, 255 );
constexpr ImU32 TextDim     = IM_COL32( 138, 146, 160, 255 );
constexpr ImU32 Warning     = IM_COL32( 245, 184, 74, 255 );
constexpr ImU32 Error       = IM_COL32( 240, 98, 90, 255 );
constexpr ImU32 Solo        = IM_COL32( 230, 130, 175, 255 );
}

// The interface fonts, compiled into the program: Inter for text, its semibold for headings, JetBrains Mono for
// numbers that change (BPM, times, values).
struct UiFonts
{
	ImFont* body    = nullptr;
	ImFont* bold    = nullptr;
	ImFont* mono    = nullptr;
	ImFont* display = nullptr; // big numbers: the BPM
};
const UiFonts& Fonts();

// Applies the style and loads the fonts sized for the display's content scale.
void ApplyTheme( float contentScale );
}
