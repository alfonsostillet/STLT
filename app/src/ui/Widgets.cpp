#include "ui/Widgets.h"
#include <algorithm>
#include <cfloat>
#include <cmath>
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
	draw->AddRectFilled( a, b, hovered ? theme::WellHover : theme::Well, style.FrameRounding );
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

void DrawDashedRect( ImDrawList* draw, ImVec2 a, ImVec2 b, ImU32 colour, float thickness, float dash )
{
	auto line = [ & ]( ImVec2 p, ImVec2 q ) {
		float length = std::hypot( q.x - p.x, q.y - p.y );
		if( length <= 0 || dash <= 0 )
			return;
		ImVec2 d( ( q.x - p.x ) / length, ( q.y - p.y ) / length );
		for( float s = 0; s < length; s += dash * 2 )
		{
			float e = std::min( s + dash, length );
			draw->AddLine( ImVec2( p.x + d.x * s, p.y + d.y * s ), ImVec2( p.x + d.x * e, p.y + d.y * e ), colour, thickness );
		}
	};
	float h = thickness / 2;
	line( ImVec2( a.x, a.y + h ), ImVec2( b.x, a.y + h ) );
	line( ImVec2( b.x - h, a.y ), ImVec2( b.x - h, b.y ) );
	line( ImVec2( b.x, b.y - h ), ImVec2( a.x, b.y - h ) );
	line( ImVec2( a.x + h, b.y ), ImVec2( a.x + h, a.y ) );
}

float TextBadge( ImDrawList* draw, ImVec2 at, const char* text, ImU32 bg, ImU32 fg, float height, bool fromRight )
{
	ImFont* font = Fonts().small;
	ImVec2 t     = font->CalcTextSizeA( font->FontSize, FLT_MAX, 0, text );
	float h      = height > 0 ? height : t.y + Ui( 2 );
	float w      = std::max( h, t.x + Ui( 6 ) );
	ImVec2 a( fromRight ? at.x - w : at.x, at.y );
	draw->AddRectFilled( a, ImVec2( a.x + w, a.y + h ), bg );
	draw->AddText( font, font->FontSize, ImVec2( a.x + ( w - t.x ) / 2, a.y + ( h - t.y ) / 2 ), fg, text );
	return w;
}
}
