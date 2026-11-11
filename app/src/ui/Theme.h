#pragma once
#include <imgui.h>

namespace stltvj
{
// The look ("Ember"): warm near-black surfaces in a few steps of depth (the deepest behind everything, panels above,
// controls above those), hairline borders, square corners, and one amber accent for whatever is live, on or
// chosen. Queued launches (waiting for their beat) are cyan, the one other colour. Text keeps at least 4.5:1
// contrast, except the faintest labels (numbers beside names).
namespace theme
{
// The interface's text size at 100 %: Ui( pixels ) sizes are pixels at that size, and grow with the zoom.
constexpr float BaseFont = 13.0f;

// Surfaces, deepest first.
constexpr ImU32 Base        = IM_COL32( 15, 14, 13, 255 );   // behind the panels
constexpr ImU32 Panel       = IM_COL32( 23, 21, 19, 255 );   // panel background
constexpr ImU32 Well        = IM_COL32( 10, 9, 8, 255 );     // sunken: fader tracks, fields, the clip pictures' ground
constexpr ImU32 Raised      = IM_COL32( 29, 27, 24, 255 );   // quiet buttons, a selected row
constexpr ImU32 RaisedHover = IM_COL32( 42, 39, 36, 255 );
constexpr ImU32 Line        = IM_COL32( 42, 39, 36, 255 );   // hairlines between blocks and cells
constexpr ImU32 Edge        = IM_COL32( 58, 54, 50, 255 );   // the border of buttons and menus
constexpr ImU32 Fill        = IM_COL32( 74, 60, 42, 255 );   // a fader's or slider's filled part

constexpr ImU32 Accent      = IM_COL32( 255, 166, 43, 255 ); // amber
constexpr ImU32 AccentDim   = IM_COL32( 122, 80, 22, 255 );
constexpr ImU32 AccentFill  = IM_COL32( 255, 166, 43, 64 );  // selections
constexpr ImU32 OnAccent    = IM_COL32( 29, 18, 3, 255 );    // text and icons on the accent
constexpr ImU32 AccentHover = IM_COL32( 190, 122, 30, 255 ); // between the dim and the full accent
constexpr ImU32 LiveStrip   = IM_COL32( 61, 42, 16, 255 );   // the name strip of a clip on show
constexpr ImU32 Queued      = IM_COL32( 92, 200, 242, 255 ); // launched, waiting for its beat or bar
constexpr ImU32 Selected    = IM_COL32( 241, 237, 231, 255 );
constexpr ImU32 CellEmpty   = IM_COL32( 10, 9, 8, 255 );
constexpr ImU32 CellBorder  = IM_COL32( 42, 39, 36, 255 );
constexpr ImU32 Text        = IM_COL32( 241, 237, 231, 255 );
constexpr ImU32 TextSoft    = IM_COL32( 195, 188, 178, 255 ); // most labels and values
constexpr ImU32 TextDim     = IM_COL32( 139, 132, 122, 255 );
constexpr ImU32 TextFaint   = IM_COL32( 101, 95, 87, 255 );   // layer numbers, captions that can be missed
constexpr ImU32 Warning     = IM_COL32( 245, 184, 74, 255 );
constexpr ImU32 Error       = IM_COL32( 240, 98, 90, 255 );
constexpr ImU32 Solo        = IM_COL32( 255, 166, 43, 255 );
}

// The interface fonts, compiled into the program: Inter for text, its semibold for headings, JetBrains Mono for
// numbers that change (BPM, times, values).
struct UiFonts
{
	ImFont* body    = nullptr;
	ImFont* bold    = nullptr;
	ImFont* mono    = nullptr;
	ImFont* display = nullptr; // big numbers: the BPM
	ImFont* small     = nullptr; // semibold, smaller: fader letters, badges
	ImFont* smallMono = nullptr; // values over the faders
};
const UiFonts& Fonts();

// Applies the style and loads the fonts sized for the display's content scale.
void ApplyTheme( float contentScale );
}
