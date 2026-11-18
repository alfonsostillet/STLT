#include "ui/TraktorUi.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include "App.h"
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
	for( int s = 0; s < (int)app.niSources.size(); ++s )
		if( app.niSources[ s ].id == id )
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

// The clips of the grid linked to `track` (the deck on show), as layer/column.
std::vector< std::pair< int, int > > LinkedClips( App& app, const nilink::TrackInfo& track )
{
	std::vector< std::pair< int, int > > found;
	nilink::TrackId id = nilink::IdOf( track );
	for( int l = 0; l < (int)app.composition.layers.size(); ++l )
	{
		const Layer& layer = app.composition.layers[ l ];
		for( int c = 0; c < (int)layer.clips.size(); ++c )
			if( !layer.clips[ c ].track.Empty() && nilink::SameTrack( layer.clips[ c ].track, id ) )
				found.emplace_back( l, c );
	}
	return found;
}

void DrawDeck( App& app, int s, int d, float width )
{
	const nilink::Source& src  = app.niSources[ s ];
	const nilink::DeckNow& now = src.decks[ d ];
	const nilink::DeckState& st = now.state;
	const UiFonts& f           = Fonts();
	ImDrawList* draw           = ImGui::GetWindowDrawList();
	bool loaded                = now.present && st.loaded;
	const nilink::TrackInfo* t = loaded && src.hasTrack[ d ] ? &src.tracks[ d ] : nullptr;
	// Narrow (the panel docked among others): the linked clip on a line of its own under the meters.
	bool narrow                = width < Ui( 600 );
	float h                    = !loaded ? Ui( 34 ) : narrow ? Ui( 114 ) : Ui( 92 );
	ImVec2 p0                  = ImGui::GetCursorScreenPos();
	ImVec2 p1( p0.x + width, p0.y + h );
	ImGui::PushID( s * 8 + d );
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
	ImGui::PopID();
	if( hovered && t && !ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
		ImGui::SetTooltip( "Drag onto a clip to link the clip to this track" );
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
	for( int k = 0; t && k < nilink::kHotcues; ++k )
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
		for( int k = 0; k < nilink::kStems; ++k )
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
	std::vector< std::pair< int, int > > linked = t ? LinkedClips( app, *t ) : std::vector< std::pair< int, int > >();
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
	auto [ l, c ]   = linked[ 0 ];
	const Clip& clip = app.composition.layers[ l ].clips[ c ];
	bool onAir      = app.composition.layers[ l ].Plays( app.composition.deck, l, c );
	char where[ 32 ];
	std::snprintf( where, sizeof( where ), "L%d \xC2\xB7 C%d%s", l + 1, c + 1, linked.size() > 1 ? " +" : "" );
	float ww = TextWidth( ImGui::GetFont(), where );
	float nameRoom   = std::max( 0.0f, x1 - ww - Ui( 8 ) - ( narrow ? x0 : sx ) - Ui( 42 ) );
	std::string name = Fit( ImGui::GetFont(), clip.name, nameRoom );
	float nw         = TextWidth( ImGui::GetFont(), name.c_str() );
	float nx         = narrow ? sx + Ui( 42 ) : x1 - ww - Ui( 8 ) - nw;
	draw->AddText( ImVec2( nx, ly ), onAir ? theme::Text : theme::TextSoft, name.c_str() );
	draw->AddText( ImVec2( narrow ? nx + nw + Ui( 8 ) : x1 - ww, ly ), theme::TextDim, where );
	// Its picture, framed in the accent while it is on air.
	GLuint thumbnail = clip.player ? clip.player->Thumbnail().Texture() : 0;
	ImVec2 i0( nx - Ui( 40 ), ly - Ui( 1 ) ), i1( nx - Ui( 6 ), ly + Ui( 19 ) );
	draw->AddRectFilled( i0, i1, IM_COL32( 0, 0, 0, 255 ) );
	if( thumbnail )
		draw->AddImage( (ImTextureID)(intptr_t)thumbnail, i0, i1, ImVec2( 0, 1 ), ImVec2( 1, 0 ) );
	if( onAir )
		draw->AddRect( ImVec2( i0.x - Ui( 1 ), i0.y - Ui( 1 ) ), ImVec2( i1.x + Ui( 1 ), i1.y + Ui( 1 ) ), theme::Accent, 0, 0, Ui( 2 ) );
}
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
	if( s < 0 || ref.deck < 0 || ref.deck >= nilink::kDecks || !app.niSources[ s ].hasTrack[ ref.deck ] || clip.Empty() )
		return false;
	const nilink::TrackInfo& t = app.niSources[ s ].tracks[ ref.deck ];
	clip.track                 = nilink::IdOf( t );
	clip.trackOffset           = 0;
	app.SetStatus( "Linked " + clip.name + " to " + clip.track.title + ( clip.track.artist.empty() ? "" : " (" + clip.track.artist + ")" ) );
	return true;
}

float DrawTrackBadge( App& app, const Clip& clip, ImDrawList* draw, ImVec2 topRight )
{
	if( clip.track.Empty() )
		return 0;
	int s, d;
	float size = Fonts().small->FontSize + Ui( 2 );
	if( clip.Playable() && app.NiDeckFor( clip, s, d ) )
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
	for( const nilink::Source& src : app.niSources )
		for( int d = 0; d < nilink::kDecks; ++d )
			any |= src.online && src.hasTrack[ d ];
	if( !any && clip.track.Empty() )
		return;
	ImGui::Separator();
	ImGui::TextDisabled( "Link to a Traktor track" );
	for( int s = 0; s < (int)app.niSources.size(); ++s )
	{
		const nilink::Source& src = app.niSources[ s ];
		for( int d = 0; d < nilink::kDecks; ++d )
		{
			if( !src.online || !src.hasTrack[ d ] )
				continue;
			const nilink::TrackInfo& t = src.tracks[ d ];
			bool linked = nilink::SameTrack( clip.track, nilink::IdOf( t ) );
			std::string label = app.NiDeckName( s, d ) + "   " + ( t.title.empty() ? "Untitled" : t.title ) + ( t.artist.empty() ? "" : "  -  " + t.artist );
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
		clip.track       = nilink::TrackId();
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
	if( clip.Playable() && app.NiDeckFor( clip, s, d ) )
	{
		const nilink::DeckNow& deck = app.niSources[ s ].decks[ d ];
		ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( DeckColour( d ) ), "deck %s", app.NiDeckName( s, d ).c_str() );
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
		clip.track       = nilink::TrackId();
		clip.trackOffset = 0;
	}
}

void DrawTraktorPanel( App& app )
{
	// The first time a DJ shows up, the panel comes to the front of its tabs.
	static bool shownOnce = false;
	bool anyDj            = std::any_of( app.niSources.begin(), app.niSources.end(), []( const nilink::Source& s ) { return s.online; } );
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
	if( !app.prefs.nilinkEnabled )
	{
		ImGui::TextDisabled( "NI LINK is off." );
		if( ImGui::Button( "Turn on" ) )
		{
			app.prefs.nilinkEnabled = true;
			app.ApplyPreferences( false );
		}
		ImGui::End();
		return;
	}
	int shown = 0;
	for( int s = 0; s < (int)app.niSources.size(); ++s )
	{
		const nilink::Source& src = app.niSources[ s ];
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
		draw->AddText( ImVec2( h0.x + Ui( 26 ) + ImGui::CalcTextSize( src.name.c_str() ).x, h0.y + Ui( 5 ) ), theme::TextDim, info );
		if( app.niLeadDeck >= 0 && app.niLeadSource == src.id && app.tempoSync.Following() == TempoSync::Source::Traktor )
		{
			char tempo[ 64 ];
			std::snprintf( tempo, sizeof( tempo ), "tempo from deck %c \xC2\xB7 %.2f", 'A' + app.niLeadDeck, app.composition.bpm );
			float tw = ImGui::CalcTextSize( tempo ).x;
			draw->AddText( ImVec2( h0.x + width - tw - Ui( 6 ), h0.y + Ui( 5 ) ), theme::TextDim, tempo );
		}
		ImGui::Dummy( ImVec2( width, hh ) );
		for( int d = 0; d < nilink::kDecks; ++d )
		{
			DrawDeck( app, s, d, width );
		}
		ImGui::Dummy( ImVec2( width, Ui( 6 ) ) );
	}
	if( !shown )
	{
		ImGui::Spacing();
		ImGui::TextDisabled( app.nilink.Running() ? "No DJ found yet." : "Not listening: the network port is taken." );
		ImGui::PushTextWrapPos( 0 );
		ImGui::TextDisabled( "On the DJ's computer, open the NI LINK transmitter next to Traktor, on the same network. "
		                     "Its decks show here by themselves (Preferences > Sync for networks without broadcast)." );
		ImGui::PopTextWrapPos();
	}
	else
	{
		ImGui::PushTextWrapPos( 0 );
		ImGui::TextDisabled( "Drag a deck onto a clip to link the clip to its track: it plays on its layer when the track plays, "
		                     "and its opacity follows the channel's fader." );
		ImGui::PopTextWrapPos();
	}
	ImGui::End();
}
}
