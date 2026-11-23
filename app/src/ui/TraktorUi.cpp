#include "ui/TraktorUi.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <imgui_stdlib.h>
#include <unordered_map>
#include <string>
#include "App.h"
#include "ui/ControlUi.h"
#include "ui/Panels.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "ui/Workspaces.h"

namespace stltvj
{
namespace
{
const ImU32 kStemColours[ 4 ] = { IM_COL32( 255, 107, 90, 255 ), IM_COL32( 255, 176, 46, 255 ), IM_COL32( 63, 208, 201, 255 ),
	                              IM_COL32( 200, 107, 255, 255 ) };
const char* kStemNames[ 4 ]   = { "DRUMS", "BASS", "OTHER", "VOX" };
const ImU32 kKey              = IM_COL32( 98, 224, 200, 255 );
const ImU32 kOnline           = IM_COL32( 169, 210, 58, 255 );

int SourceIndex( const App& app, uint32_t id )
{
	for( int s = 0; s < (int)app.linkSources.size(); ++s )
		if( app.linkSources[ s ].id == id )
			return s;
	return -1;
}

std::string Clock( double seconds, bool remaining )
{
	seconds = std::max( 0.0, seconds );
	int whole = (int)seconds;
	char text[ 24 ];
	std::snprintf( text, sizeof( text ), "%s%d:%02d", remaining ? "-" : "", whole / 60, whole % 60 );
	return text;
}

// Text cut to `width` with an ellipsis.
std::string Fit( ImFont* font, const std::string& text, float width )
{
	auto w = [ & ]( const std::string& s ) { return font->CalcTextSizeA( font->FontSize, FLT_MAX, 0, s.c_str() ).x; };
	if( w( text ) <= width )
		return text;
	std::string t = text;
	while( !t.empty() )
	{
		t.pop_back();
		while( !t.empty() && ( (unsigned char)t.back() & 0xC0 ) == 0x80 )
			t.pop_back();
		if( w( t + "..." ) <= width )
			return t + "...";
	}
	return "";
}

float TextWidth( ImFont* font, const char* text )
{
	return font->CalcTextSizeA( font->FontSize, FLT_MAX, 0, text ).x;
}

// Two links of a chain: a clip linked to a track that no deck has now.
void LinkSign( ImDrawList* draw, ImVec2 c, float s, ImU32 colour )
{
	float w = s * 0.42f, h = s * 0.26f, r = h;
	draw->AddRect( ImVec2( c.x - w, c.y - h - s * 0.08f ), ImVec2( c.x + w * 0.2f, c.y + h - s * 0.08f ), colour, r, 0, std::max( 1.0f, s * 0.11f ) );
	draw->AddRect( ImVec2( c.x - w * 0.2f, c.y - h + s * 0.08f ), ImVec2( c.x + w, c.y + h + s * 0.08f ), colour, r, 0, std::max( 1.0f, s * 0.11f ) );
}

// A cell of the grid, in any deck of clips.
struct Cell
{
	int deck, row, column;
};

// The clips linked to `track`, in every deck of clips (the deck on show first).
std::vector< Cell > LinkedClips( App& app, const nimslink::TrackId& id )
{
	std::vector< Cell > found;
	app.composition.ForEachClip( [ & ]( Clip& clip, int d, int r, int c ) {
		if( !clip.track.Empty() && nimslink::SameTrack( clip.track, id ) )
			found.push_back( { d, r, c } );
	} );
	std::stable_partition( found.begin(), found.end(), [ & ]( const Cell& c ) { return c.deck == app.composition.deck; } );
	return found;
}

// "L1 · C2", with the deck of clips when it is not the one on show ("L1 · C2 · Deck 2"); "+" when there are more.
std::string Where( const App& app, const Cell& cell, bool more )
{
	std::string text = "L" + std::to_string( cell.row + 1 ) + " \xC2\xB7 C" + std::to_string( cell.column + 1 );
	if( cell.deck != app.composition.deck && cell.deck < (int)app.composition.decks.size() )
		text += " \xC2\xB7 " + app.composition.decks[ cell.deck ].name;
	return text + ( more ? " +" : "" );
}

// A clip's picture, framed in the accent while it is on air.
void Thumbnail( ImDrawList* draw, const Clip& clip, ImVec2 i0, ImVec2 i1, bool onAir )
{
	draw->AddRectFilled( i0, i1, IM_COL32( 0, 0, 0, 255 ) );
	GLuint texture = clip.player ? clip.player->Thumbnail().Texture() : 0;
	if( texture )
		draw->AddImage( (ImTextureID)(intptr_t)texture, i0, i1, ImVec2( 0, 1 ), ImVec2( 1, 0 ) );
	else if( clip.generatorThumbnail )
		draw->AddImage( (ImTextureID)(intptr_t)clip.generatorThumbnail->Texture(), i0, i1, ImVec2( 0, 1 ), ImVec2( 1, 0 ) );
	if( onAir )
		draw->AddRect( ImVec2( i0.x - Ui( 1 ), i0.y - Ui( 1 ) ), ImVec2( i1.x + Ui( 1 ), i1.y + Ui( 1 ) ), theme::Accent, 0, 0, Ui( 2 ) );
}

// A small X at `at` (its top left); true when clicked.
bool UnlinkButton( const char* id, ImVec2 at, const char* tip )
{
	ImVec2 keep = ImGui::GetCursorScreenPos();
	ImGui::SetCursorScreenPos( at );
	bool clicked = IconButton( id, Icon::Close, false, Ui( 18 ) );
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "%s", tip );
	ImGui::SetCursorScreenPos( keep );
	return clicked;
}

void DrawDeckBody( App& app, int s, int d, float width, ImVec2 p0, float h, bool hovered );

void DrawDeck( App& app, int s, int d, float width )
{
	const nimslink::Source& src  = app.linkSources[ s ];
	const nimslink::DeckNow& now = src.decks[ d ];
	bool loaded                  = now.present && now.state.loaded;
	const nimslink::TrackInfo* t = loaded && src.hasTrack[ d ] ? &src.tracks[ d ] : nullptr;
	// Narrow (the panel docked among others): the linked clip on a line of its own under the meters.
	bool narrow = width < Ui( 600 );
	float h     = !loaded ? Ui( 34 ) : narrow ? Ui( 114 ) : Ui( 92 );
	ImVec2 p0   = ImGui::GetCursorScreenPos();
	ImGui::PushID( s * 8 + d );
	ImGui::SetNextItemAllowOverlap(); // the X that unlinks the track's clip sits on it
	ImGui::InvisibleButton( "deck", ImVec2( width, h ) );
	bool hovered = ImGui::IsItemHovered();
	// Dragged onto a clip of the grid: the clip is linked to this deck's track.
	if( t && ImGui::BeginDragDropSource() )
	{
		DeckRef ref{ src.id, d };
		ImGui::SetDragDropPayload( kDeckPayload, &ref, sizeof( ref ) );
		ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( DeckColour( d ) ), "%c", 'A' + d );
		ImGui::SameLine();
		ImGui::Text( "%s%s%s", t->title.c_str(), t->artist.empty() ? "" : "  -  ", t->artist.c_str() );
		ImGui::EndDragDropSource();
	}
	if( hovered && t && !ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
		ImGui::SetTooltip( "Drag onto a clip to link the clip to this track" );
	DrawDeckBody( app, s, d, width, p0, h, hovered );
	ImGui::PopID();
}

void DrawDeckBody( App& app, int s, int d, float width, ImVec2 p0, float h, bool hovered )
{
	const nimslink::Source& src   = app.linkSources[ s ];
	const nimslink::DeckNow& now  = src.decks[ d ];
	const nimslink::DeckState& st = now.state;
	const UiFonts& f              = Fonts();
	ImDrawList* draw              = ImGui::GetWindowDrawList();
	bool loaded                   = now.present && st.loaded;
	const nimslink::TrackInfo* t  = loaded && src.hasTrack[ d ] ? &src.tracks[ d ] : nullptr;
	bool narrow                   = width < Ui( 600 );
	ImVec2 p1( p0.x + width, p0.y + h );
	draw->AddRectFilled( p0, p1, hovered && t ? theme::Raised : theme::Panel );
	draw->AddLine( ImVec2( p0.x, p1.y - 0.5f ), ImVec2( p1.x, p1.y - 0.5f ), theme::Line );
	draw->AddRectFilled( p0, ImVec2( p0.x + Ui( 3 ), p1.y ), loaded ? DeckColour( d ) : theme::Line );
	char letter[ 2 ] = { (char)( 'A' + d ), 0 };
	draw->AddText( f.display, f.display->FontSize, ImVec2( p0.x + Ui( 10 ), p0.y + Ui( 6 ) ), loaded ? DeckColour( d ) : theme::TextFaint, letter );
	float x0 = p0.x + Ui( 34 ), x1 = p1.x - Ui( 10 );
	if( !loaded )
	{
		draw->AddText( ImVec2( x0, p0.y + ( h - ImGui::GetTextLineHeight() ) / 2 ), theme::TextFaint, now.present ? "empty" : "-" );
		return;
	}
	// First line: state, title, artist; on the right, time left, BPM and key.
	float y = p0.y + Ui( 8 );
	const char* state = st.playing ? "PLAY" : "CUE";
	float sw          = TextBadge( draw, ImVec2( x0, y + Ui( 1 ) ), state, st.playing ? theme::Accent : theme::Raised, st.playing ? theme::OnAccent : theme::TextDim );
	char right[ 64 ];
	std::string key = t ? t->key : "";
	std::snprintf( right, sizeof( right ), "%s   %.2f", Clock( ( t ? t->length : 0 ) - now.position, true ).c_str(), now.bpm );
	float kw = key.empty() ? 0 : TextWidth( f.mono, key.c_str() ) + Ui( 10 );
	float rw = TextWidth( f.mono, right );
	draw->AddText( f.mono, f.mono->FontSize, ImVec2( x1 - kw - rw, y ), theme::TextSoft, right );
	if( !key.empty() )
		draw->AddText( f.mono, f.mono->FontSize, ImVec2( x1 - kw + Ui( 10 ), y ), kKey, key.c_str() );
	float tx = x0 + sw + Ui( 8 ), room = x1 - kw - rw - Ui( 12 ) - tx;
	std::string title = Fit( f.bold, t && !t->title.empty() ? t->title : "Untitled", room * 0.7f );
	draw->AddText( f.bold, f.bold->FontSize, ImVec2( tx, y ), theme::Text, title.c_str() );
	float tw = TextWidth( f.bold, title.c_str() ) + Ui( 8 );
	if( t && !t->artist.empty() )
		draw->AddText( ImVec2( tx + tw, y ), theme::TextDim, Fit( ImGui::GetFont(), t->artist, room - tw ).c_str() );

	// The track: played part, hotcues (blue; loops green), the loop playing, the playhead.
	y += Ui( 24 );
	ImVec2 b0( x0, y ), b1( x1, y + Ui( 14 ) );
	draw->AddRectFilled( b0, b1, theme::Well );
	double length = t && t->length > 0 ? t->length : 0;
	auto xAt      = [ & ]( double seconds ) { return b0.x + ( b1.x - b0.x ) * (float)std::clamp( length > 0 ? seconds / length : 0.0, 0.0, 1.0 ); };
	draw->AddRectFilled( b0, ImVec2( xAt( now.position ), b1.y ), IM_COL32( 42, 36, 28, 255 ) );
	if( st.inLoop && st.loopLength > 0 )
		draw->AddRectFilled( ImVec2( xAt( st.loopStart ), b0.y ), ImVec2( xAt( st.loopStart + st.loopLength ), b1.y ), IM_COL32( 169, 210, 58, 60 ) );
	for( int k = 0; t && k < nimslink::kHotcues; ++k )
		if( t->hotcues[ k ].exists && t->hotcues[ k ].type != 4 ) // AutoGrid markers aside
		{
			float hx = xAt( t->hotcues[ k ].start );
			draw->AddLine( ImVec2( hx, b0.y ), ImVec2( hx, b1.y ), t->hotcues[ k ].type == 5 ? kOnline : IM_COL32( 61, 149, 245, 255 ), Ui( 2 ) );
		}
	float px = xAt( now.position );
	draw->AddLine( ImVec2( px, b0.y - Ui( 2 ) ), ImVec2( px, b1.y + Ui( 2 ) ), theme::Accent, Ui( 2 ) );

	// The stems' levels (or the channel's, without stems); on the right, the clips linked to the track.
	y += Ui( 24 );
	float sx = x0;
	ImFont* small = f.small;
	if( t && t->stems )
		for( int k = 0; k < nimslink::kStems; ++k )
		{
			draw->AddText( small, small->FontSize, ImVec2( sx, y ), theme::TextFaint, kStemNames[ k ] );
			sx += TextWidth( small, kStemNames[ k ] ) + Ui( 4 );
			float my = y + small->FontSize / 2 - Ui( 2 );
			draw->AddRectFilled( ImVec2( sx, my ), ImVec2( sx + Ui( 32 ), my + Ui( 5 ) ), theme::Well );
			draw->AddRectFilled( ImVec2( sx, my ), ImVec2( sx + Ui( 32 ) * std::clamp( st.stems[ k ], 0.0f, 1.0f ), my + Ui( 5 ) ), kStemColours[ k ] );
			sx += Ui( 40 );
		}
	else
	{
		draw->AddText( small, small->FontSize, ImVec2( sx, y ), theme::TextFaint, "LEVEL" );
		sx += TextWidth( small, "LEVEL" ) + Ui( 4 );
		float my = y + small->FontSize / 2 - Ui( 2 );
		draw->AddRectFilled( ImVec2( sx, my ), ImVec2( sx + Ui( 60 ), my + Ui( 5 ) ), theme::Well );
		draw->AddRectFilled( ImVec2( sx, my ), ImVec2( sx + Ui( 60 ) * std::clamp( st.level, 0.0f, 1.0f ), my + Ui( 5 ) ), theme::Fill );
		sx += Ui( 68 );
	}
	char fader[ 24 ];
	std::snprintf( fader, sizeof( fader ), "FADER %d%%", (int)std::lround( st.volume * 100 ) );
	if( sx + Ui( 4 ) + TextWidth( small, fader ) <= x1 )
		draw->AddText( small, small->FontSize, ImVec2( sx + Ui( 4 ), y ), theme::TextFaint, fader );
	sx += TextWidth( small, fader ) + Ui( 14 );
	std::vector< Cell > linked = t ? LinkedClips( app, nimslink::IdOf( *t ) ) : std::vector< Cell >();
	float ly = y - Ui( 3 );
	if( narrow )
	{
		ly = y + Ui( 20 );
		sx = x0;
	}
	if( linked.empty() )
	{
		std::string hint = Fit( ImGui::GetFont(), "no clip: drag the deck onto a clip", x1 - sx );
		draw->AddText( ImVec2( narrow ? sx : x1 - TextWidth( ImGui::GetFont(), hint.c_str() ), ly ), theme::TextFaint, hint.c_str() );
		return;
	}
	// The linked clip and its place; at the end the X that unlinks it.
	Cell cell        = linked[ 0 ];
	const Clip* clip = app.composition.ClipIn( cell.deck, cell.row, cell.column );
	if( !clip )
		return;
	bool onAir       = app.composition.LayerShowingIn( cell.deck, cell.row, cell.column ) >= 0;
	float xw         = Ui( 18 ) * 1.3f + Ui( 6 );
	float rightEdge  = x1 - xw;
	std::string where = Where( app, cell, linked.size() > 1 );
	float ww          = TextWidth( ImGui::GetFont(), where.c_str() );
	float nameRoom    = std::max( 0.0f, rightEdge - ww - Ui( 8 ) - ( narrow ? x0 : sx ) - Ui( 42 ) );
	std::string name  = Fit( ImGui::GetFont(), clip->name, nameRoom );
	float nw          = TextWidth( ImGui::GetFont(), name.c_str() );
	float nx          = narrow ? sx + Ui( 42 ) : rightEdge - ww - Ui( 8 ) - nw;
	draw->AddText( ImVec2( nx, ly ), onAir ? theme::Text : theme::TextSoft, name.c_str() );
	draw->AddText( ImVec2( nx + nw + Ui( 8 ), ly ), theme::TextDim, where.c_str() );
	Thumbnail( draw, *clip, ImVec2( nx - Ui( 40 ), ly - Ui( 1 ) ), ImVec2( nx - Ui( 6 ), ly + Ui( 19 ) ), onAir );
	if( UnlinkButton( "##unlink", ImVec2( x1 - Ui( 18 ) * 1.3f, ly - Ui( 1 ) ), "Unlink: this track has no clip any more (link another by dragging the deck onto it)" ) )
		app.UnlinkTrack( nimslink::IdOf( *t ) );
}

}

void DrawNimsSwitch( App& app )
{
	bool on = app.nimsActive;
	if( on )
	{
		ImGui::PushStyleColor( ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4( theme::AccentDim ) );
		ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4( theme::AccentHover ) );
	}
	ImGui::PushFont( Fonts().bold );
	if( ImGui::Button( "NIMS###nims" ) )
		app.SetNimsActive( !on );
	ImGui::PopFont();
	if( on )
		ImGui::PopStyleColor( 2 );
	MapTarget( app, "/composition/nims" );
	if( ImGui::IsItemHovered() && !app.learning )
		ImGui::SetTooltip( on ? "NIMS PRO LINK on: each layer plays the clip linked to the track the DJ plays, at its place, with the "
		                        "fader.\nLaunch or stop a clip by hand on such a layer and NIMS goes off: the layers are yours."
		                      : "NIMS PRO LINK off: the layers are yours, Traktor launches nothing.\nPress to give them back: every "
		                        "linked clip comes back at its track's place." );
}

ImU32 DeckColour( int deck )
{
	static const ImU32 colours[ 4 ] = { IM_COL32( 255, 90, 79, 255 ), IM_COL32( 61, 149, 245, 255 ), IM_COL32( 95, 211, 90, 255 ),
		                                IM_COL32( 255, 208, 46, 255 ) };
	return colours[ ( deck % 4 + 4 ) % 4 ];
}

bool LinkClipToDeck( App& app, Clip& clip, const DeckRef& ref )
{
	int s = SourceIndex( app, ref.source );
	if( s < 0 || ref.deck < 0 || ref.deck >= nimslink::kDecks || !app.linkSources[ s ].hasTrack[ ref.deck ] || clip.Empty() )
		return false;
	app.LinkClipToTrack( clip, nimslink::IdOf( app.linkSources[ s ].tracks[ ref.deck ] ) );
	return true;
}

float DrawTrackBadge( App& app, const Clip& clip, ImDrawList* draw, ImVec2 topRight )
{
	if( clip.track.Empty() )
		return 0;
	int s, d;
	float size = Fonts().small->FontSize + Ui( 2 );
	if( clip.Playable() && app.LinkDeckFor( clip, s, d ) )
	{
		char letter[ 2 ] = { (char)( 'A' + d ), 0 };
		return TextBadge( draw, topRight, letter, DeckColour( d ), IM_COL32( 17, 17, 17, 255 ), size, true );
	}
	// Linked, but no deck has the track now.
	ImVec2 a( topRight.x - size, topRight.y );
	draw->AddRectFilled( a, ImVec2( topRight.x, topRight.y + size ), IM_COL32( 0, 0, 0, 190 ) );
	LinkSign( draw, ImVec2( a.x + size / 2, a.y + size / 2 ), size * 0.8f, theme::TextSoft );
	return size;
}

void TrackMenuItems( App& app, Clip& clip )
{
	if( clip.Empty() )
		return;
	bool any = false;
	for( const nimslink::Source& src : app.linkSources )
		for( int d = 0; d < nimslink::kDecks; ++d )
			any |= src.online && src.hasTrack[ d ];
	if( !any && clip.track.Empty() )
		return;
	ImGui::Separator();
	ImGui::TextDisabled( "Link to a Traktor track" );
	for( int s = 0; s < (int)app.linkSources.size(); ++s )
	{
		const nimslink::Source& src = app.linkSources[ s ];
		for( int d = 0; d < nimslink::kDecks; ++d )
		{
			if( !src.online || !src.hasTrack[ d ] )
				continue;
			const nimslink::TrackInfo& t = src.tracks[ d ];
			bool linked = nimslink::SameTrack( clip.track, nimslink::IdOf( t ) );
			std::string label = app.LinkDeckName( s, d ) + "   " + ( t.title.empty() ? "Untitled" : t.title ) + ( t.artist.empty() ? "" : "  -  " + t.artist );
			ImGui::PushID( s * 8 + d );
			ImGui::PushStyleColor( ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4( DeckColour( d ) ) );
			bool picked = ImGui::MenuItem( label.c_str(), nullptr, linked );
			ImGui::PopStyleColor();
			ImGui::PopID();
			if( picked && !linked )
				LinkClipToDeck( app, clip, { src.id, d } );
		}
	}
	if( ImGui::MenuItem( "Unlink from Traktor", nullptr, false, !clip.track.Empty() ) )
	{
		clip.track       = nimslink::TrackId();
		clip.trackOffset = 0;
	}
}

void TrackSection( App& app, Clip& clip )
{
	if( clip.track.Empty() )
		return;
	ImGui::SeparatorText( "Traktor" );
	ImGui::TextDisabled( "Track" );
	ImGui::SameLine( Ui( 90 ) );
	ImGui::TextUnformatted( clip.track.title.c_str() );
	ImGui::SameLine();
	std::string extra = ( clip.track.artist.empty() ? "" : clip.track.artist + "  " ) + ( clip.track.length > 0 ? Clock( clip.track.length, false ) : "" );
	ImGui::TextDisabled( "%s", extra.c_str() );
	int s, d;
	ImGui::TextDisabled( "Now" );
	ImGui::SameLine( Ui( 90 ) );
	if( clip.Playable() && app.LinkDeckFor( clip, s, d ) )
	{
		const nimslink::DeckNow& deck = app.linkSources[ s ].decks[ d ];
		ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( DeckColour( d ) ), "deck %s", app.LinkDeckName( s, d ).c_str() );
		ImGui::SameLine();
		ImGui::Text( "%s  %s  fader %d%%", deck.state.playing ? "playing" : "paused", Clock( deck.position, false ).c_str(), (int)std::lround( deck.state.volume * 100 ) );
	}
	else
		ImGui::TextDisabled( "not on a deck" );
	ImGui::TextDisabled( "Movie" );
	ImGui::SameLine( Ui( 90 ) );
	double length = clip.RangeEnd() - clip.RangeStart();
	if( clip.track.length > 0 && length > 0 )
		ImGui::TextUnformatted( length >= clip.track.length * 0.5 ? "follows the track" : "loops with the track (shorter than half of it)" );
	else
		ImGui::TextUnformatted( "follows the track" );
	ImGui::TextDisabled( "Offset" );
	ImGui::SameLine( Ui( 90 ) );
	float offset = (float)clip.trackOffset;
	ImGui::SetNextItemWidth( Ui( 160 ) );
	if( ImGui::DragFloat( "##trackOffset", &offset, 0.01f, -600.0f, 600.0f, "%+.2f s" ) )
		clip.trackOffset = offset;
	if( ResetValue( &offset, 0 ) )
		clip.trackOffset = 0;
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "Seconds added to the track's position: the movie a little ahead (+) or behind (-)" );
	if( ImGui::Button( "Unlink" ) )
	{
		clip.track       = nimslink::TrackId();
		clip.trackOffset = 0;
	}
}

bool LinkClipToDraggedTrack( App& app, Clip& clip, const void* data, int size )
{
	if( size != (int)sizeof( TrackDrag ) || clip.Empty() )
		return false;
	TrackDrag drag;
	std::memcpy( &drag, data, sizeof( drag ) );
	drag.title[ sizeof( drag.title ) - 1 ]   = 0;
	drag.artist[ sizeof( drag.artist ) - 1 ] = 0;
	nimslink::TrackId id;
	id.title  = drag.title;
	id.artist = drag.artist;
	id.length = drag.length;
	if( id.Empty() )
		return false;
	app.LinkClipToTrack( clip, id );
	return true;
}

namespace
{
// A line of the Track Links panel: a track, the clips linked to it, the deck that has it now.
struct LinkRow
{
	nimslink::TrackId id;
	std::string key; // nimslink::TrackKey
	double bpm = 0;
	std::string musicKey;
	bool remembered = false; // in the list of tracks seen (else only linked in this composition)
	std::vector< Cell > clips;
	int source = -1, deck = -1; // a deck that has it now
	bool playing = false;
};

bool Lengths( double a, double b )
{
	return a <= 0 || b <= 0 || std::fabs( a - b ) < 1.0; // as nimslink::SameTrack
}

std::string Lower( const std::string& text )
{
	std::string out = text;
	for( char& c : out )
		c = (char)std::tolower( (unsigned char)c );
	return out;
}

std::vector< LinkRow > BuildRows( App& app )
{
	std::vector< LinkRow > rows;
	for( const nimslink::LibraryTrack& t : app.trackLibrary.Tracks() )
	{
		LinkRow row;
		row.id         = t.id;
		row.key        = nimslink::TrackKey( t.id );
		row.bpm        = t.bpm;
		row.musicKey   = t.key;
		row.remembered = true;
		rows.push_back( std::move( row ) );
	}
	// The lines by key, to find a track's line at once among thousands.
	std::unordered_multimap< std::string, size_t > byKey;
	for( size_t i = 0; i < rows.size(); ++i )
		byKey.emplace( rows[ i ].key, i );
	auto find = [ & ]( const std::string& key, double length ) -> LinkRow* {
		auto [ from, to ] = byKey.equal_range( key );
		for( auto it = from; it != to; ++it )
			if( Lengths( rows[ it->second ].id.length, length ) )
				return &rows[ it->second ];
		return nullptr;
	};
	// The composition's links: on their track's line, or on one of their own (a track linked elsewhere, or forgotten).
	app.composition.ForEachClip( [ & ]( Clip& clip, int d, int r, int c ) {
		if( clip.track.Empty() || clip.Empty() )
			return;
		std::string key = nimslink::TrackKey( clip.track );
		if( LinkRow* row = find( key, clip.track.length ) )
		{
			row->clips.push_back( { d, r, c } );
			return;
		}
		LinkRow row;
		row.id  = clip.track;
		row.key = key;
		row.clips.push_back( { d, r, c } );
		rows.push_back( std::move( row ) );
		byKey.emplace( key, rows.size() - 1 );
	} );
	// The decks that have them now (one playing first).
	for( int s = 0; s < (int)app.linkSources.size(); ++s )
	{
		const nimslink::Source& src = app.linkSources[ s ];
		for( int d = 0; src.online && d < nimslink::kDecks; ++d )
		{
			if( !src.hasTrack[ d ] || !src.decks[ d ].present || !src.decks[ d ].state.loaded )
				continue;
			nimslink::TrackId id = nimslink::IdOf( src.tracks[ d ] );
			LinkRow* row         = find( nimslink::TrackKey( id ), id.length );
			if( row && ( row->deck < 0 || ( !row->playing && src.decks[ d ].state.playing ) ) )
			{
				row->source  = s;
				row->deck    = d;
				row->playing = src.decks[ d ].state.playing;
			}
		}
	}
	for( LinkRow& row : rows )
		std::stable_partition( row.clips.begin(), row.clips.end(), [ & ]( const Cell& c ) { return c.deck == app.composition.deck; } );
	std::sort( rows.begin(), rows.end(), []( const LinkRow& a, const LinkRow& b ) { return a.key < b.key; } );
	return rows;
}
}

void DrawTrackLinksPanel( App& app )
{
	ImGui::SetNextWindowSize( ImVec2( Ui( 620 ), Ui( 460 ) ), ImGuiCond_FirstUseEver );
	if( !ImGui::Begin( panel::TrackLinks, &app.panels.trackLinks ) )
	{
		ImGui::End();
		return;
	}
	static std::string query;
	static int filter = 0; // 0 every track, 1 linked, 2 not linked
	std::vector< LinkRow > all = BuildRows( app );
	int linkedCount            = (int)std::count_if( all.begin(), all.end(), []( const LinkRow& r ) { return !r.clips.empty(); } );

	// On one line when the panel is wide enough, else carried over to the next.
	float lineEnd = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
	auto next     = [ & ]( float width ) {
		ImGui::SameLine();
		if( ImGui::GetCursorScreenPos().x + width > lineEnd )
			ImGui::NewLine();
	};
	auto radioWidth = []( const char* label ) { return ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x + ImGui::CalcTextSize( label ).x; };
	ImGui::SetNextItemWidth( std::min( Ui( 260 ), ImGui::GetContentRegionAvail().x ) );
	ImGui::InputTextWithHint( "##search", "Search title or artist", &query );
	next( radioWidth( "All" ) );
	ImGui::RadioButton( "All", &filter, 0 );
	next( radioWidth( "Linked" ) );
	ImGui::RadioButton( "Linked", &filter, 1 );
	next( radioWidth( "Not linked" ) );
	ImGui::RadioButton( "Not linked", &filter, 2 );
	char count[ 64 ];
	std::snprintf( count, sizeof( count ), "%d tracks, %d linked", (int)all.size(), linkedCount );
	next( ImGui::CalcTextSize( count ).x );
	ImGui::TextDisabled( "%s", count );

	std::string needle = Lower( query );
	std::vector< const LinkRow* > rows;
	for( const LinkRow& r : all )
	{
		if( ( filter == 1 && r.clips.empty() ) || ( filter == 2 && !r.clips.empty() ) )
			continue;
		if( !needle.empty() && Lower( r.id.title ).find( needle ) == std::string::npos && Lower( r.id.artist ).find( needle ) == std::string::npos )
			continue;
		rows.push_back( &r );
	}
	if( all.empty() )
	{
		ImGui::Spacing();
		ImGui::PushTextWrapPos( 0 );
		ImGui::TextDisabled( "No tracks yet. Every track the DJ loads in Traktor (NIMS PRO LINK) is added here and kept for the next "
		                     "time, so you can link clips to it without the DJ playing it again." );
		ImGui::PopTextWrapPos();
		ImGui::End();
		return;
	}
	ImGui::TextDisabled( "Drag a track onto a clip to link it; the X unlinks. A track has one clip." );

	const UiFonts& f = Fonts();
	float rowH       = Ui( 40 );
	ImGui::BeginChild( "##tracks", ImVec2( 0, 0 ) );
	ImDrawList* draw = ImGui::GetWindowDrawList();
	float width      = ImGui::GetContentRegionAvail().x;
	ImGuiListClipper clipper;
	clipper.Begin( (int)rows.size(), rowH );
	while( clipper.Step() )
		for( int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i )
		{
			const LinkRow& r = *rows[ i ];
			ImGui::PushID( r.key.c_str() );
			ImGui::PushID( (int)std::lround( r.id.length ) );
			ImVec2 p0 = ImGui::GetCursorScreenPos();
			ImVec2 p1( p0.x + width, p0.y + rowH );
			ImGui::SetNextItemAllowOverlap(); // the X sits on it
			ImGui::InvisibleButton( "track", ImVec2( width, rowH ) );
			bool hovered = ImGui::IsItemHovered();
			if( ImGui::BeginDragDropSource() )
			{
				TrackDrag drag{};
				if( r.id.title.size() < sizeof( drag.title ) && r.id.artist.size() < sizeof( drag.artist ) )
				{
					std::memcpy( drag.title, r.id.title.c_str(), r.id.title.size() + 1 );
					std::memcpy( drag.artist, r.id.artist.c_str(), r.id.artist.size() + 1 );
					drag.length = r.id.length;
					ImGui::SetDragDropPayload( kTrackPayload, &drag, sizeof( drag ) );
				}
				ImGui::Text( "%s%s%s", r.id.title.c_str(), r.id.artist.empty() ? "" : "  -  ", r.id.artist.c_str() );
				ImGui::EndDragDropSource();
			}
			if( ImGui::BeginPopupContextItem( "menu" ) )
			{
				if( ImGui::MenuItem( "Unlink", nullptr, false, !r.clips.empty() ) )
					app.UnlinkTrack( r.id );
				if( ImGui::MenuItem( "Forget this track", nullptr, false, r.remembered ) )
				{
					app.trackLibrary.Forget( r.id );
					app.SaveTrackLibrary();
				}
				ImGui::EndPopup();
			}
			if( hovered && r.clips.empty() && !ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
				ImGui::SetTooltip( "Drag onto a clip to link it to this track" );
			draw->AddRectFilled( p0, p1, hovered ? theme::Raised : theme::Panel );
			draw->AddLine( ImVec2( p0.x, p1.y - 0.5f ), ImVec2( p1.x, p1.y - 0.5f ), theme::Line );
			// The deck that has it now, in its colour (filled while it plays).
			float x = p0.x + Ui( 8 );
			if( r.deck >= 0 )
			{
				char letter[ 2 ] = { (char)( 'A' + r.deck ), 0 };
				if( r.playing )
					TextBadge( draw, ImVec2( x, p0.y + Ui( 11 ) ), letter, DeckColour( r.deck ), IM_COL32( 17, 17, 17, 255 ) );
				else
					draw->AddText( f.bold, f.bold->FontSize, ImVec2( x + Ui( 3 ), p0.y + Ui( 11 ) ), DeckColour( r.deck ), letter );
			}
			x += Ui( 24 );
			// On the right, the clip and the X; the title takes what is left.
			float right = p1.x - Ui( 8 );
			if( !r.clips.empty() )
			{
				const Clip* clip = app.composition.ClipIn( r.clips[ 0 ].deck, r.clips[ 0 ].row, r.clips[ 0 ].column );
				if( clip )
				{
					float xb = right - Ui( 18 ) * 1.3f;
					if( UnlinkButton( "##unlink", ImVec2( xb, p0.y + ( rowH - Ui( 18 ) ) / 2 ), "Unlink: this track has no clip any more" ) )
						app.UnlinkTrack( r.id );
					std::string where = Where( app, r.clips[ 0 ], r.clips.size() > 1 );
					float ww          = TextWidth( ImGui::GetFont(), where.c_str() );
					float room        = std::max( Ui( 40 ), ( width - Ui( 32 ) ) * 0.45f - ww - Ui( 60 ) );
					std::string name  = Fit( ImGui::GetFont(), clip->name, room );
					float nw          = TextWidth( ImGui::GetFont(), name.c_str() );
					float ty          = p0.y + ( rowH - ImGui::GetTextLineHeight() ) / 2;
					float wx          = xb - Ui( 8 ) - ww;
					float nx          = wx - Ui( 8 ) - nw;
					bool onAir        = app.composition.LayerShowingIn( r.clips[ 0 ].deck, r.clips[ 0 ].row, r.clips[ 0 ].column ) >= 0;
					draw->AddText( ImVec2( wx, ty ), theme::TextDim, where.c_str() );
					draw->AddText( ImVec2( nx, ty ), onAir ? theme::Text : theme::TextSoft, name.c_str() );
					Thumbnail( draw, *clip, ImVec2( nx - Ui( 40 ), p0.y + ( rowH - Ui( 20 ) ) / 2 ), ImVec2( nx - Ui( 6 ), p0.y + ( rowH + Ui( 20 ) ) / 2 ), onAir );
					right = nx - Ui( 48 );
				}
			}
			float room        = std::max( 0.0f, right - x - Ui( 8 ) );
			std::string title = Fit( f.bold, r.id.title.empty() ? "Untitled" : r.id.title, room );
			draw->AddText( f.bold, f.bold->FontSize, ImVec2( x, p0.y + Ui( 4 ) ), theme::Text, title.c_str() );
			std::string second = r.id.artist;
			if( r.id.length > 0 )
				second += ( second.empty() ? "" : "   " ) + Clock( r.id.length, false );
			if( r.bpm > 0 )
			{
				char bpm[ 24 ];
				std::snprintf( bpm, sizeof( bpm ), "%.2f BPM", r.bpm );
				second += ( second.empty() ? "" : "   " ) + std::string( bpm );
			}
			if( !r.musicKey.empty() )
				second += "   " + r.musicKey;
			draw->AddText( ImVec2( x, p0.y + Ui( 21 ) ), theme::TextDim, Fit( ImGui::GetFont(), second, room ).c_str() );
			ImGui::PopID();
			ImGui::PopID();
		}
	clipper.End();
	ImGui::EndChild();
	ImGui::End();
}

void DrawTraktorPanel( App& app )
{
	// The first time a DJ shows up, the panel comes to the front of its tabs.
	static bool shownOnce = false;
	bool anyDj            = std::any_of( app.linkSources.begin(), app.linkSources.end(), []( const nimslink::Source& s ) { return s.online; } );
	if( anyDj && !shownOnce )
	{
		ImGui::SetNextWindowFocus();
		shownOnce = true;
	}
	if( !ImGui::Begin( panel::Traktor, &app.panels.traktor ) )
	{
		ImGui::End();
		return;
	}
	ImDrawList* draw = ImGui::GetWindowDrawList();
	float width      = ImGui::GetContentRegionAvail().x;
	if( !app.prefs.nimslinkEnabled )
	{
		ImGui::TextDisabled( "NIMS PRO LINK is off." );
		if( ImGui::Button( "Turn on" ) )
		{
			app.prefs.nimslinkEnabled = true;
			app.ApplyPreferences( false );
		}
		ImGui::End();
		return;
	}
	DrawNimsSwitch( app );
	ImGui::SameLine();
	ImGui::AlignTextToFramePadding();
	ImGui::TextDisabled( app.nimsActive ? "the linked clips follow the tracks" : "off: the layers are yours" );
	ImGui::Spacing();
	int shown = 0;
	for( int s = 0; s < (int)app.linkSources.size(); ++s )
	{
		const nimslink::Source& src = app.linkSources[ s ];
		if( !src.online )
			continue;
		++shown;
		// The DJ's computer: a light, its name, Traktor and the network's delay; the tempo, when a deck of it leads.
		ImVec2 h0 = ImGui::GetCursorScreenPos();
		float hh  = ImGui::GetTextLineHeight() + Ui( 10 );
		bool sending = !src.program.empty();
		draw->AddCircleFilled( ImVec2( h0.x + Ui( 8 ), h0.y + hh / 2 ), Ui( 3.5f ), sending ? kOnline : theme::Warning );
		draw->AddText( ImVec2( h0.x + Ui( 18 ), h0.y + Ui( 5 ) ), theme::Text, src.name.c_str() );
		char info[ 96 ];
		std::snprintf( info, sizeof( info ), "%s \xC2\xB7 %.1f ms", sending ? src.program.c_str() : "Traktor not sending", src.roundTrip * 1000 / 2 );
		float infoX = h0.x + Ui( 26 ) + ImGui::CalcTextSize( src.name.c_str() ).x;
		draw->AddText( ImVec2( infoX, h0.y + Ui( 5 ) ), theme::TextDim, info );
		if( app.linkLeadDeck >= 0 && app.linkLeadSource == src.id && app.tempoSync.Following() == TempoSync::Source::Traktor )
		{
			char tempo[ 64 ];
			std::snprintf( tempo, sizeof( tempo ), "tempo from deck %c \xC2\xB7 %.2f", 'A' + app.linkLeadDeck, app.composition.bpm );
			float tw = ImGui::CalcTextSize( tempo ).x;
			// On the same line when there is room, under it otherwise (a narrow panel).
			bool below = infoX + ImGui::CalcTextSize( info ).x + Ui( 16 ) > h0.x + width - tw - Ui( 6 );
			ImVec2 at  = below ? ImVec2( h0.x + Ui( 18 ), h0.y + hh ) : ImVec2( h0.x + width - tw - Ui( 6 ), h0.y + Ui( 5 ) );
			draw->AddText( at, theme::TextDim, tempo );
			if( below )
				hh += ImGui::GetTextLineHeight() + Ui( 4 );
		}
		ImGui::Dummy( ImVec2( width, hh ) );
		for( int d = 0; d < nimslink::kDecks; ++d )
		{
			DrawDeck( app, s, d, width );
		}
		ImGui::Dummy( ImVec2( width, Ui( 6 ) ) );
	}
	if( !shown )
	{
		ImGui::Spacing();
		ImGui::TextDisabled( app.nimslink.Running() ? "No DJ found yet." : "Not listening: the network port is taken." );
		ImGui::PushTextWrapPos( 0 );
		ImGui::TextDisabled( "On the DJ's computer, open the NIMS PRO LINK transmitter next to Traktor, on the same network. "
		                     "Its decks show here by themselves (Preferences > Sync for networks without broadcast)." );
		ImGui::PopTextWrapPos();
	}
	else
	{
		ImGui::PushTextWrapPos( 0 );
		ImGui::TextDisabled( "Drag a deck onto a clip to link the clip to its track: it plays on its layer when the track plays, "
		                     "and its opacity follows the channel's fader. Launch or stop a clip by hand on that layer and NIMS goes "
		                     "off; press NIMS to bring the linked clips back. Every track the decks have had is in Track Links "
		                     "(View menu)." );
		ImGui::PopTextWrapPos();
	}
	ImGui::End();
}
}
