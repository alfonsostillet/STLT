#pragma once
#include <imgui.h>

namespace stltvj
{
// The palette: near-black panels, grey controls and one teal accent for whatever is live or selected.
namespace theme
{
constexpr ImU32 Accent      = IM_COL32( 102, 225, 192, 255 );
constexpr ImU32 AccentDim   = IM_COL32( 56, 120, 104, 255 );
constexpr ImU32 Selected    = IM_COL32( 230, 230, 230, 255 );
constexpr ImU32 CellEmpty   = IM_COL32( 30, 30, 30, 255 );
constexpr ImU32 CellBorder  = IM_COL32( 46, 46, 46, 255 );
constexpr ImU32 TextDim     = IM_COL32( 150, 150, 150, 255 );
constexpr ImU32 Error       = IM_COL32( 230, 90, 80, 255 );
constexpr ImU32 Solo        = IM_COL32( 214, 120, 160, 255 );
}

// Applies the style and loads a UI font sized for the display's content scale.
void ApplyTheme( float contentScale );
}
