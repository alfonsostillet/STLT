#include "ui/Widgets.h"
#include <algorithm>
#include "ui/Theme.h"

namespace stltvj
{
bool FilledSlider( const char* label, float* value, float min, float max, const char* format, ImGuiSliderFlags flags )
{
	// The frame is drawn here, under ImGui's own slider (whose background is made transparent): where the slider
	// will be is known before it is drawn, so no draw list channels are needed (tables use them).
	const ImGuiStyle& style = ImGui::GetStyle();
	ImDrawList* draw        = ImGui::GetWindowDrawList();
	ImVec2 a                = ImGui::GetCursorScreenPos();
	ImVec2 b( a.x + ImGui::CalcItemWidth(), a.y + ImGui::GetFrameHeight() );
	bool hovered = ImGui::IsMouseHoveringRect( a, b ) && ImGui::IsWindowHovered();
	draw->AddRectFilled( a, b, hovered ? IM_COL32( 22, 20, 18, 255 ) : theme::Well, style.FrameRounding );
	if( max > min )
	{
		auto at      = [ & ]( float v ) { return a.x + ( b.x - a.x ) * std::clamp( ( v - min ) / ( max - min ), 0.0f, 1.0f ); };
		float origin = at( min < 0 && max > 0 ? 0.0f : min );
		float end    = at( *value );
		float left = std::min( origin, end ), right = std::max( origin, end );
		if( right - left >= 1 )
		{
			ImDrawFlags corners = ImDrawFlags_RoundCornersNone;
			if( left <= a.x + 0.5f )
				corners |= ImDrawFlags_RoundCornersLeft;
			if( right >= b.x - 0.5f )
				corners |= ImDrawFlags_RoundCornersRight;
			draw->AddRectFilled( ImVec2( left, a.y ), ImVec2( right, b.y ), theme::Fill, style.FrameRounding, corners );
		}
	}
	ImGui::PushStyleColor( ImGuiCol_FrameBg, IM_COL32( 0, 0, 0, 0 ) );
	ImGui::PushStyleColor( ImGuiCol_FrameBgHovered, IM_COL32( 0, 0, 0, 0 ) );
	ImGui::PushStyleColor( ImGuiCol_FrameBgActive, IM_COL32( 255, 255, 255, 10 ) );
	ImGui::PushStyleVar( ImGuiStyleVar_GrabMinSize, std::max( 2.0f, style.GrabMinSize * 0.25f ) ); // the grab: a thin line at the value
	bool changed = ImGui::SliderFloat( label, value, min, max, format, flags );
	ImGui::PopStyleVar();
	ImGui::PopStyleColor( 3 );
	return changed;
}

bool ResetValue( float* value, float reset )
{
	if( !ImGui::IsItemHovered() || !( ImGui::IsMouseClicked( ImGuiMouseButton_Right ) || ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) ) )
		return false;
	*value = reset;
	return true;
}

bool CommittedInputInts( const char* label, int* values, int count )
{
	// While the fields are in use they edit a copy, which keeps what is typed across frames; it is handed over when
	// the fields let go after an edit. One copy is enough: only one field can be in use at a time.
	static ImGuiID editing = 0;
	static int typed[ 4 ];
	count            = std::clamp( count, 1, 4 );
	const ImGuiID id = ImGui::GetID( label );
	if( editing != id )
		std::copy( values, values + count, typed );
	ImGui::InputScalarN( label, ImGuiDataType_S32, typed, count, nullptr, nullptr, "%d" );
	if( ImGui::IsItemActive() )
		editing = id;
	else if( editing == id )
		editing = 0;
	if( !ImGui::IsItemDeactivatedAfterEdit() || std::equal( typed, typed + count, values ) )
		return false;
	std::copy( typed, typed + count, values );
	return true;
}
}
