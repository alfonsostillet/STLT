#pragma once
#include <imgui.h>

namespace stltvj
{
// A slider drawn as a bar filled up to its value with the value written across it, as live video and audio
// software does: readable at a glance from across a dark room. When the range spans zero the fill starts at zero
// (brightness, contrast...). Same arguments and behaviour as ImGui::SliderFloat (drag, Ctrl+click to type).
bool FilledSlider( const char* label, float* value, float min, float max, const char* format = "%.3f", ImGuiSliderFlags flags = 0 );
// A right-click or double-click on the last item puts `value` back to its default, as in Resolume. Returns
// whether it did.
bool ResetValue( float* value, float reset );
// One to four whole numbers typed in fields side by side, given to `values` only once the typing is done (Enter, Tab
// out of the fields or a click elsewhere), not at every key: what EnterReturnsTrue did before ImGui stopped allowing
// it on number fields. Returns whether `values` changed.
bool CommittedInputInts( const char* label, int* values, int count );
}
