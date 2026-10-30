#include "ui/TimelineUi.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
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
float Ui( float pixels )
{
	return pixels * ImGui::GetFontSize() / 15.0f;
}

// Drag payloads of the other panels: a deck cell, a file of the Files panel.
constexpr const char* kCellPayload = "STLT_CELL";
constexpr const char* kFilePayload = "STLT_FILE";
struct CellRef
{
	int layer, column;
};

// Block colours: strong enough to tell songs apart from across the room, dark enough for white names on them.
const ImU32 kBlockColours[] = { IM_COL32( 46, 125, 110, 255 ), IM_COL32( 160, 92, 40, 255 ), IM_COL32( 84, 92, 170, 255 ), IM_COL32( 150, 60, 110, 255 ),
	                            IM_COL32( 110, 130, 40, 255 ), IM_COL32( 40, 110, 160, 255 ), IM_COL32( 150, 50, 50, 255 ), IM_COL32( 100, 100, 110, 255 ) };
constexpr int kColourCount = (int)( sizeof( kBlockColours ) / sizeof( kBlockColours[ 0 ] ) );

ImU32 BlockColour( int index, int alpha = 255 )
{
	ImU32 c = kBlockColours[ ( ( index % kColourCount ) + kColourCount ) % kColourCount ];
	return ( c & 0x00FFFFFF ) | ( (ImU32)alpha << 24 );
}

enum class Snap
{
	Off,
	Beat,
	Bar
};

enum class Drag
{
	None,
	Move,       // a clip: moved (or copied with Alt) on release
	TrimIn,     // a clip's start (and media in point)
	TrimOut,    // a clip's end
	BlockMove,
	BlockStart,
	BlockEnd,
	BlockNew,   // a new block drawn on the block lane
	Scrub,      // the playhead, on the ruler
};

// What the panel remembers between frames.
struct State
{
	float zoom = 40; // pixels per second (at 15 px type)
	Snap snap  = Snap::Beat;
	bool follow = true; // the view follows the playhead while it plays
	int track = -1, clip = -1; // the selected clip
	int block = -1;            // the selected block
	Drag drag = Drag::None;
	int dragTrack = -1, dragIndex = -1;
	double grab = 0; // where in the clip or block it was grabbed (seconds from its start)
	double origStart = 0, origLength = 0, origIn = 0;
	bool copy       = false;
	int targetTrack = -1;
	double targetStart = 0;
	double newStart    = 0, newEnd = 0;
	float scrollX      = -1; // a scroll to apply (-1: none)
} state;

double Snapped( const Timeline& t, double time, bool free )
{
	time = std::max( 0.0, time );
	if( free || state.snap == Snap::Off )
		return time;
	double unit = state.snap == Snap::Bar ? t.SecondsPerBar() : t.SecondsPerBeat();
	return std::round( time / unit ) * unit;
}

void SelectClip( int track, int clip )
{
	state.track = track;
	state.clip  = clip;
	state.block = -1;
}

TimelineClip* SelectedClip( Timeline& t )
{
	if( state.track < 0 || state.track >= (int)t.tracks.size() || state.clip < 0 || state.clip >= (int)t.tracks[ state.track ].clips.size() )
		return nullptr;
	return &t.tracks[ state.track ].clips[ state.clip ];
}

// A copy of a timeline clip right after it (or at `at`).
void Duplicate( App& app, int track, int index, double at = -1 )
{
	Timeline& t = app.composition.timeline;
	if( track < 0 || track >= (int)t.tracks.size() || index < 0 || index >= (int)t.tracks[ track ].clips.size() )
		return;
	const TimelineClip& from = t.tracks[ track ].clips[ index ];
	TimelineClip copy;
	copy.clip   = CopyClip( from.clip );
	copy.start  = at >= 0 ? at : from.End();
	copy.length = from.length;
	int placed  = t.Place( track, std::move( copy ) );
	SelectClip( track, placed );
}

void DeleteSelection( App& app )
{
	Timeline& t = app.composition.timeline;
	if( TimelineClip* clip = SelectedClip( t ) )
	{
		app.SetStatus( "Removed " + clip->clip.name + " from the timeline" );
		t.tracks[ state.track ].clips.erase( t.tracks[ state.track ].clips.begin() + state.clip );
		state.clip = -1;
	}
	else if( state.block >= 0 && state.block < (int)t.blocks.size() )
	{
		t.blocks.erase( t.blocks.begin() + state.block );
		state.block = -1;
	}
}

// Splits the selected clip at the playhead, or every clip under it when none is selected.
void SplitAtPlayhead( App& app )
{
	Timeline& t = app.composition.timeline;
	bool any    = false;
	if( SelectedClip( t ) )
		any = t.Split( state.track, state.clip, t.position );
	else
		for( int k = 0; k < (int)t.tracks.size(); ++k )
		{
			int index = t.tracks[ k ].ClipAt( t.position );
			any |= index >= 0 && t.Split( k, index, t.position );
		}
	app.SetStatus( any ? "Split at " + t.BarsBeats( t.position ) : "Nothing to split under the playhead" );
}

// A new block from the playhead: up to the next block, or eight bars.
void AddBlock( App& app )
{
	Timeline& t  = app.composition.timeline;
	double start = Snapped( t, t.position, false );
	int at       = t.BlockAt( start );
	if( at >= 0 )
		start = t.blocks[ at ].end;
	double end = start + t.SecondsPerBar() * 8;
	for( const TimelineBlock& b : t.blocks )
		if( b.start >= start )
			end = std::min( end, b.start );
	if( end - start < 0.05 )
	{
		app.SetStatus( "No room for a block here: move the playhead to a free place", true );
		return;
	}
	TimelineBlock block;
	block.name   = "Song " + std::to_string( t.blocks.size() + 1 );
	block.start  = start;
	block.end    = end;
	block.colour = (int)t.blocks.size();
	t.blocks.push_back( block );
	t.SortBlocks();
	for( int i = 0; i < (int)t.blocks.size(); ++i )
		if( t.blocks[ i ].start == start )
			state.block = i;
	state.clip = -1;
}

void AddTrack( App& app, bool audio )
{
	Timeline& t = app.composition.timeline;
	int count   = 1;
	for( const TimelineTrack& k : t.tracks )
		count += k.audio == audio ? 1 : 0;
	TimelineTrack track;
	track.audio = audio;
	track.name  = ( audio ? "Audio " : "Video " ) + std::to_string( count );
	// Video tracks go on top of the video tracks (under the audio ones); audio tracks at the top.
	if( audio )
		t.tracks.push_back( std::move( track ) );
	else
	{
		int at = 0;
		for( int i = 0; i < (int)t.tracks.size(); ++i )
			if( !t.tracks[ i ].audio )
				at = i + 1;
		t.tracks.insert( t.tracks.begin() + at, std::move( track ) );
		if( state.track >= at )
			++state.track;
	}
}

bool SmallToggle( const char* label, bool on, ImU32 colour, const char* tip )
{
	ImGui::PushStyleColor( ImGuiCol_Button, on ? ImGui::ColorConvertU32ToFloat4( colour ) : ImGui::GetStyleColorVec4( ImGuiCol_Button ) );
	ImGui::PushStyleColor( ImGuiCol_Text, on ? ImGui::ColorConvertU32ToFloat4( theme::OnAccent ) : ImGui::GetStyleColorVec4( ImGuiCol_Text ) );
	bool pressed = ImGui::SmallButton( label );
	ImGui::PopStyleColor( 2 );
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "%s", tip );
	return pressed;
}

// ---- top: transport, position, tempo, snap, zoom; then the cues

void Toolbar( App& app )
{
	Timeline& t = app.composition.timeline;
	if( IconButton( "##tlStart", Icon::Previous ) )
		t.Locate( 0 );
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "To the start" );
	ImGui::SameLine();
	if( IconButton( "##tlPlay", t.playing ? Icon::Pause : Icon::Play, t.playing ) )
		app.TimelinePlay( !t.playing );
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "Play / pause the timeline (Space)" );
	MapTarget( app, "/timeline/play" );
	ImGui::SameLine();
	if( IconButton( "##tlStop", Icon::Stop ) )
		app.TimelineStop();
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "Stop; again: back to the start" );
	MapTarget( app, "/timeline/stop" );

	// Where the playhead is: bars and beats, and the show's timecode.
	ImGui::SameLine( 0, Ui( 14 ) );
	ImGui::PushFont( Fonts().mono );
	ImGui::AlignTextToFramePadding();
	ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( t.black ? theme::Warning : theme::Accent ), "%-7s", t.BarsBeats( t.position ).c_str() );
	ImGui::SameLine();
	ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( theme::Text ), "%s", t.Timecode( t.position ).c_str() );
	ImGui::PopFont();
	if( t.black )
	{
		ImGui::SameLine();
		ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( theme::Warning ), "BLACK" );
	}

	ImGui::SameLine( 0, Ui( 14 ) );
	ImGui::SetNextItemWidth( Ui( 78 ) );
	float bpm = (float)t.bpm;
	if( ImGui::DragFloat( "##tlBpm", &bpm, 0.1f, 20.0f, 500.0f, "%.2f BPM" ) )
		t.bpm = std::clamp( (double)bpm, 20.0, 500.0 );
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "The show's tempo: the ruler's bars and beats and the snap (the musicians' click)" );
	ImGui::SameLine();
	ImGui::SetNextItemWidth( Ui( 54 ) );
	char metre[ 48 ];
	std::snprintf( metre, sizeof( metre ), "%d/4", t.beatsPerBar );
	if( ImGui::BeginCombo( "##tlMetre", metre ) )
	{
		for( int beats : { 2, 3, 4, 5, 6, 7, 8, 12 } )
		{
			std::snprintf( metre, sizeof( metre ), "%d/4", beats );
			if( ImGui::Selectable( metre, t.beatsPerBar == beats ) )
				t.beatsPerBar = beats;
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth( Ui( 72 ) );
	char fps[ 48 ];
	std::snprintf( fps, sizeof( fps ), "%g fps", t.fps );
	if( ImGui::BeginCombo( "##tlFps", fps ) )
	{
		for( double rate : { 24.0, 25.0, 29.97, 30.0, 50.0, 60.0 } )
		{
			std::snprintf( fps, sizeof( fps ), "%g fps", rate );
			if( ImGui::Selectable( fps, t.fps == rate ) )
				t.fps = rate;
		}
		ImGui::EndCombo();
	}
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "Frames per second of the timecode" );

	ImGui::SameLine( 0, Ui( 14 ) );
	ImGui::SetNextItemWidth( Ui( 82 ) );
	const char* snaps[] = { "Snap: Off", "Snap: Beat", "Snap: Bar" };
	if( ImGui::BeginCombo( "##tlSnap", snaps[ (int)state.snap ] ) )
	{
		for( int i = 0; i < 3; ++i )
			if( ImGui::Selectable( snaps[ i ], (int)state.snap == i ) )
				state.snap = (Snap)i;
		ImGui::EndCombo();
	}
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "Clips and blocks land on beats or bars (hold Shift while dragging to place freely)" );
	ImGui::SameLine();
	ImGui::SetNextItemWidth( Ui( 90 ) );
	ImGui::SliderFloat( "##tlZoom", &state.zoom, 2.0f, 400.0f, "Zoom %.0f", ImGuiSliderFlags_Logarithmic );
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "Zoom (or Ctrl + mouse wheel over the tracks)" );
	ImGui::SameLine();
	ImGui::Checkbox( "Follow", &state.follow );
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "The view follows the playhead while it plays" );

	ImGui::SameLine( 0, Ui( 14 ) );
	if( ImGui::Button( "+ Video Track" ) )
		AddTrack( app, false );
	ImGui::SameLine();
	if( ImGui::Button( "+ Audio Track" ) )
		AddTrack( app, true );
	ImGui::SameLine();
	if( ImGui::Button( "+ Block" ) )
		AddBlock( app );
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "A block (a song) from the playhead: its cue starts it, its end action decides what follows.\nOr drag on the block lane." );
}

void CueStrip( App& app )
{
	Timeline& t = app.composition.timeline;
	ImGui::AlignTextToFramePadding();
	ImGui::TextDisabled( "Cues" );
	ImGui::SameLine();
	if( IconButton( "##tlPrevCue", Icon::Previous ) )
		app.TimelineNextCue( -1 );
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "Previous cue (Left arrow)" );
	MapTarget( app, "/timeline/prevcue" );
	ImGui::SameLine();
	if( IconButton( "##tlNextCue", Icon::Next ) )
		app.TimelineNextCue( 1 );
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "Next cue (Right arrow)" );
	MapTarget( app, "/timeline/nextcue" );
	if( t.blocks.empty() )
	{
		ImGui::SameLine();
		ImGui::TextDisabled( "Add blocks (+ Block, or drag on the block lane): each block is a cue." );
		return;
	}
	int at = t.BlockAt( t.position );
	for( int i = 0; i < (int)t.blocks.size(); ++i )
	{
		const TimelineBlock& b = t.blocks[ i ];
		ImGui::SameLine();
		ImGui::PushID( i );
		char label[ 160 ];
		std::snprintf( label, sizeof( label ), "%d  %s  (%s)", i + 1, b.name.c_str(), BlockEndName( b.endAction ) );
		bool current = i == at;
		ImGui::PushStyleColor( ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4( BlockColour( b.colour, current ? 255 : 150 ) ) );
		ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4( BlockColour( b.colour ) ) );
		if( ImGui::Button( label ) )
			app.TimelineCue( i );
		ImGui::PopStyleColor( 2 );
		if( current )
		{
			ImVec2 a = ImGui::GetItemRectMin(), c = ImGui::GetItemRectMax();
			ImGui::GetWindowDrawList()->AddRect( a, c, theme::Selected, ImGui::GetStyle().FrameRounding, 0, Ui( 2 ) );
		}
		if( ImGui::IsItemHovered() && !app.learning )
			ImGui::SetTooltip( "Cue %d: %s from %s, then %s", i + 1, b.name.c_str(), t.BarsBeats( b.start ).c_str(),
			                   b.endAction == BlockEnd::Next   ? "plays on"
			                   : b.endAction == BlockEnd::Loop ? "loops"
			                   : b.endAction == BlockEnd::Hold ? "holds its last frame"
			                                                   : "goes to black" );
		MapTarget( app, "/timeline/cue/" + std::to_string( i + 1 ) );
		ImGui::PopID();
	}
}

// ---- the tracks

void TrackHeader( App& app, int k, ImVec2 pos, ImVec2 size )
{
	Timeline& t          = app.composition.timeline;
	TimelineTrack& track = t.tracks[ k ];
	ImDrawList* draw     = ImGui::GetWindowDrawList();
	draw->AddRectFilled( pos, ImVec2( pos.x + size.x, pos.y + size.y - Ui( 1 ) ), track.audio ? IM_COL32( 28, 34, 30, 255 ) : theme::Raised );
	ImGui::PushID( k );
	ImGui::SetCursorScreenPos( ImVec2( pos.x + Ui( 6 ), pos.y + Ui( 4 ) ) );
	ImGui::PushFont( Fonts().bold );
	ImGui::TextUnformatted( track.name.c_str() );
	ImGui::PopFont();
	if( ImGui::IsItemClicked( ImGuiMouseButton_Right ) || ( ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) ) )
		ImGui::OpenPopup( "##trackMenu" );
	ImGui::SetCursorScreenPos( ImVec2( pos.x + size.x - Ui( 46 ), pos.y + Ui( 3 ) ) );
	if( SmallToggle( "M", track.mute, theme::Warning, "Mute: the track off" ) )
		track.mute = !track.mute;
	ImGui::SameLine( 0, Ui( 2 ) );
	if( SmallToggle( "S", track.solo, theme::Solo, "Solo: only this track (and other soloed ones)" ) )
		track.solo = !track.solo;
	if( size.y > Ui( 34 ) )
	{
		ImGui::SetCursorScreenPos( ImVec2( pos.x + Ui( 6 ), pos.y + size.y - Ui( 24 ) ) );
		ImGui::SetNextItemWidth( size.x - Ui( 12 ) );
		if( track.audio )
		{
			float percent = track.volume * 100;
			if( FilledSlider( "##volume", &percent, 0, 100, "Volume %.0f %%" ) )
				track.volume = percent / 100;
			ResetValue( &track.volume, 1.0f );
		}
		else
		{
			float percent = track.opacity * 100;
			if( FilledSlider( "##opacity", &percent, 0, 100, "Opacity %.0f %%" ) )
				track.opacity = percent / 100;
			ResetValue( &track.opacity, 1.0f );
		}
	}
	// Right-click on a free part of the header: the track's menu too.
	ImGui::SetCursorScreenPos( pos );
	ImGui::SetNextItemAllowOverlap();
	ImGui::InvisibleButton( "##header", ImVec2( size.x, size.y - Ui( 1 ) ) );
	if( ImGui::IsItemClicked( ImGuiMouseButton_Right ) )
		ImGui::OpenPopup( "##trackMenu" );
	if( ImGui::BeginPopup( "##trackMenu" ) )
	{
		ImGui::SetNextItemWidth( Ui( 180 ) );
		ImGui::InputText( "Name", &track.name );
		if( !track.audio )
		{
			ImGui::SetNextItemWidth( Ui( 180 ) );
			if( ImGui::BeginCombo( "Blend", BlendModeName( track.blend ) ) )
			{
				for( int b = 0; b < (int)BlendMode::Count; ++b )
					if( ImGui::Selectable( BlendModeName( (BlendMode)b ), (int)track.blend == b ) )
						track.blend = (BlendMode)b;
				ImGui::EndCombo();
			}
		}
		ImGui::Separator();
		if( ImGui::MenuItem( "Delete Track" ) )
		{
			t.tracks.erase( t.tracks.begin() + k );
			state.clip = state.track = -1;
			ImGui::EndPopup();
			ImGui::PopID();
			return;
		}
		ImGui::EndPopup();
	}
	ImGui::PopID();
}

void Lanes( App& app, float height )
{
	Timeline& t      = app.composition.timeline;
	const ImGuiIO& io = ImGui::GetIO();
	const float headerW = Ui( 170 ), rulerH = Ui( 22 ), blockH = Ui( 26 ), topH = rulerH + blockH;
	const float videoH = Ui( 60 ), audioH = Ui( 42 ), edge = Ui( 6 );
	const float pps = state.zoom * ImGui::GetFontSize() / 15.0f; // pixels per second
	double length   = t.Length();
	double shown    = std::max( length + 60.0, 120.0 );
	float contentW  = headerW + (float)( shown * pps );
	float tracksH   = 0;
	for( const TimelineTrack& k : t.tracks )
		tracksH += k.audio ? audioH : videoH;

	ImGui::BeginChild( "##timeline", ImVec2( 0, height ), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	if( state.scrollX >= 0 )
	{
		ImGui::SetScrollX( state.scrollX );
		state.scrollX = -1;
	}
	ImDrawList* draw = ImGui::GetWindowDrawList();
	ImVec2 origin    = ImGui::GetCursorScreenPos();
	ImVec2 winPos    = ImGui::GetWindowPos();
	ImVec2 winSize   = ImGui::GetWindowSize();
	float scrollX = ImGui::GetScrollX(), scrollY = ImGui::GetScrollY();
	float stickyX = origin.x + scrollX, stickyY = origin.y + scrollY;
	float laneLeft = stickyX + headerW, laneTop = stickyY + topH;
	float viewRight = winPos.x + winSize.x, viewBottom = winPos.y + winSize.y;
	float x0 = origin.x + headerW; // where time 0 is
	auto X = [ & ]( double time ) { return x0 + (float)( time * pps ); };
	auto timeAt = [ & ]( float x ) { return std::max( 0.0, (double)( x - x0 ) / pps ); };
	ImGui::Dummy( ImVec2( contentW, topH + tracksH + Ui( 40 ) ) );

	// Ctrl + wheel zooms around the mouse; the wheel alone scrolls up and down, Shift + wheel sideways.
	bool overLanes = ImGui::IsWindowHovered( ImGuiHoveredFlags_ChildWindows ) && io.MousePos.x >= laneLeft;
	if( overLanes && io.MouseWheel != 0 )
	{
		if( io.KeyCtrl )
		{
			double at   = timeAt( io.MousePos.x );
			state.zoom  = std::clamp( state.zoom * ( io.MouseWheel > 0 ? 1.2f : 1 / 1.2f ), 2.0f, 400.0f );
			float npps  = state.zoom * ImGui::GetFontSize() / 15.0f;
			state.scrollX = std::max( 0.0f, (float)( at * npps ) - ( io.MousePos.x - laneLeft ) );
		}
		else if( io.KeyShift )
			ImGui::SetScrollX( std::max( 0.0f, scrollX - io.MouseWheel * Ui( 60 ) ) );
		else
			ImGui::SetScrollY( std::max( 0.0f, scrollY - io.MouseWheel * Ui( 40 ) ) );
	}
	// Following the playhead: a page on when it leaves the view.
	if( state.follow && t.playing && state.drag == Drag::None )
	{
		float px = X( t.position );
		if( px > viewRight - Ui( 30 ) || px < laneLeft )
			state.scrollX = std::max( 0.0f, (float)( t.position * pps ) - Ui( 40 ) );
	}

	// Lanes: track backgrounds with bar lines, top track first (the last in the list).
	std::vector< float > trackY( t.tracks.size() ), trackH( t.tracks.size() );
	float y = origin.y + topH;
	for( int k = (int)t.tracks.size() - 1; k >= 0; --k )
	{
		trackY[ k ] = y;
		trackH[ k ] = t.tracks[ k ].audio ? audioH : videoH;
		y += trackH[ k ];
	}
	auto trackAt = [ & ]( float my ) {
		for( int k = 0; k < (int)t.tracks.size(); ++k )
			if( my >= trackY[ k ] && my < trackY[ k ] + trackH[ k ] )
				return k;
		return -1;
	};
	draw->PushClipRect( ImVec2( laneLeft, laneTop ), ImVec2( viewRight, viewBottom ), true );
	for( int k = 0; k < (int)t.tracks.size(); ++k )
	{
		draw->AddRectFilled( ImVec2( laneLeft, trackY[ k ] ), ImVec2( viewRight, trackY[ k ] + trackH[ k ] - Ui( 1 ) ),
		                     t.tracks[ k ].audio ? IM_COL32( 18, 23, 20, 255 ) : theme::Well );
	}
	// Bar lines (beats too when there is room).
	{
		double bar = t.SecondsPerBar(), beat = t.SecondsPerBeat();
		bool beats = beat * pps > Ui( 14 );
		double step = beats ? beat : bar;
		// Far out, every 2, 4, 8... bars.
		while( step * pps < Ui( 14 ) )
			step *= 2;
		double first = std::floor( timeAt( laneLeft ) / step ) * step;
		for( double time = first; X( time ) < viewRight; time += step )
		{
			bool isBar = std::fabs( std::remainder( time, bar ) ) < 1e-6;
			draw->AddLine( ImVec2( X( time ), laneTop ), ImVec2( X( time ), origin.y + topH + tracksH ), isBar ? IM_COL32( 255, 255, 255, 26 ) : IM_COL32( 255, 255, 255, 10 ) );
		}
	}
	// Blocks shade the tracks under them.
	for( const TimelineBlock& b : t.blocks )
		draw->AddRectFilled( ImVec2( X( b.start ), laneTop ), ImVec2( X( b.end ), origin.y + topH + tracksH ), BlockColour( b.colour, 18 ) );
	draw->PopClipRect();

	// Clips. Each is a button over its visible part: the edges trim, the body moves (Alt: a copy).
	bool freeDrag = io.KeyShift;
	for( int k = 0; k < (int)t.tracks.size(); ++k )
	{
		TimelineTrack& track = t.tracks[ k ];
		for( int i = 0; i < (int)track.clips.size(); ++i )
		{
			TimelineClip& tc = track.clips[ i ];
			ImVec2 a( X( tc.start ), trackY[ k ] + Ui( 2 ) ), b( X( tc.End() ), trackY[ k ] + trackH[ k ] - Ui( 3 ) );
			bool dragged = state.drag == Drag::Move && state.dragTrack == k && state.dragIndex == i;
			bool selected = state.track == k && state.clip == i;
			// Visible part: right of the headers, under the ruler.
			ImVec2 va( std::max( a.x, laneLeft ), std::max( a.y, laneTop ) ), vb( std::min( b.x, viewRight ), std::min( b.y, viewBottom ) );
			if( vb.x - va.x >= 1 && vb.y - va.y >= 1 )
			{
				draw->PushClipRect( ImVec2( laneLeft, laneTop ), ImVec2( viewRight, viewBottom ), true );
				ImU32 body = track.audio ? IM_COL32( 46, 92, 64, 255 ) : IM_COL32( 52, 70, 96, 255 );
				if( !tc.clip.Loaded() && !tc.clip.generator && !tc.clip.audio )
					body = IM_COL32( 110, 40, 44, 255 ); // missing media
				draw->AddRectFilled( a, b, dragged && !state.copy ? ( body & 0x00FFFFFF ) | 0x60000000 : body, Ui( 3 ) );
				// The picture at its start, and the name.
				GLuint thumb = tc.clip.player && tc.clip.Loaded() ? tc.clip.player->Thumbnail().Texture()
				               : tc.clip.generatorThumbnail                ? tc.clip.generatorThumbnail->Texture()
				                                                           : 0;
				float th = b.y - a.y - Ui( 16 );
				float tw = th * 16 / 9;
				if( thumb && !track.audio && th > Ui( 12 ) && b.x - a.x > tw + Ui( 4 ) )
					draw->AddImage( (ImTextureID)(intptr_t)thumb, ImVec2( a.x + Ui( 2 ), a.y + Ui( 14 ) ), ImVec2( a.x + Ui( 2 ) + tw, a.y + Ui( 14 ) + th ), ImVec2( 0, 1 ),
					                ImVec2( 1, 0 ) );
				if( track.audio )
				{
					// A sound: a simple wave pattern, so it reads as audio.
					float mid = ( a.y + b.y ) / 2 + Ui( 6 );
					for( float x = std::max( a.x, laneLeft ) + Ui( 2 ); x < std::min( b.x, viewRight ) - Ui( 2 ); x += Ui( 3 ) )
					{
						float h = Ui( 3 ) + Ui( 9 ) * (float)std::fabs( std::sin( ( x - a.x ) * 0.11f ) * std::cos( ( x - a.x ) * 0.037f ) );
						draw->AddLine( ImVec2( x, mid - h ), ImVec2( x, mid + h ), IM_COL32( 140, 220, 160, 150 ) );
					}
				}
				draw->PushClipRect( ImVec2( std::max( a.x, laneLeft ), a.y ), ImVec2( std::min( b.x, viewRight ), b.y ), true );
				draw->AddText( ImVec2( std::max( a.x, laneLeft ) + Ui( 4 ), a.y + Ui( 1 ) ), theme::Text, tc.clip.name.c_str() );
				draw->PopClipRect();
				draw->AddRect( a, b, selected ? theme::Selected : IM_COL32( 0, 0, 0, 120 ), Ui( 3 ), 0, selected ? Ui( 2 ) : Ui( 1 ) );
				draw->PopClipRect();

				ImGui::PushID( k * 100000 + i );
				ImGui::SetCursorScreenPos( va );
				ImGui::InvisibleButton( "##clip", ImVec2( vb.x - va.x, vb.y - va.y ) );
				bool hovered = ImGui::IsItemHovered();
				float mx     = io.MousePos.x;
				bool atIn = mx - a.x < edge && b.x - a.x > edge * 3, atOut = b.x - mx < edge && b.x - a.x > edge * 3;
				if( hovered && ( atIn || atOut ) && state.drag == Drag::None )
					ImGui::SetMouseCursor( ImGuiMouseCursor_ResizeEW );
				if( ImGui::IsItemActivated() )
				{
					SelectClip( k, i );
					state.drag       = atIn ? Drag::TrimIn : atOut ? Drag::TrimOut : Drag::Move;
					state.dragTrack  = k;
					state.dragIndex  = i;
					state.grab       = timeAt( mx ) - tc.start;
					state.origStart  = tc.start;
					state.origLength = tc.length;
					state.origIn     = tc.clip.inPoint;
					state.copy       = io.KeyAlt;
					state.targetTrack = k;
					state.targetStart = tc.start;
				}
				if( hovered && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
					t.Locate( tc.start );
				if( hovered && state.drag == Drag::None && ImGui::IsItemHovered( ImGuiHoveredFlags_DelayNormal ) && !app.learning )
					ImGui::SetTooltip( "%s\n%s - %s (%.2f s)\nDrag: move (Alt: copy, Shift: off the grid); edges: trim\nDouble-click: playhead here; "
					                   "right-click: menu",
					                   tc.clip.name.c_str(), t.BarsBeats( tc.start ).c_str(), t.BarsBeats( tc.End() ).c_str(), tc.length );
				if( ImGui::BeginPopupContextItem( "##clipMenu" ) )
				{
					SelectClip( k, i );
					if( ImGui::MenuItem( "Duplicate", "Ctrl+D" ) )
						Duplicate( app, k, i );
					if( ImGui::MenuItem( "Split at Playhead", "Ctrl+E", false, t.position > tc.start && t.position < tc.End() ) )
						t.Split( k, i, t.position );
					bool loop = tc.clip.playMode == PlayMode::Loop;
					if( ImGui::MenuItem( "Loop the Media", nullptr, loop ) )
						tc.clip.playMode = loop ? PlayMode::Once : PlayMode::Loop;
					if( ImGui::IsItemHovered() )
						ImGui::SetTooltip( "When the clip is longer than its media: start the media again, or hold its last frame" );
					if( ImGui::MenuItem( "Fit to Media" ) && tc.MediaDuration() > 0 )
					{
						double out = tc.clip.outPoint >= 0 ? tc.clip.outPoint : tc.MediaDuration();
						tc.length  = std::max( 0.05, ( out - tc.clip.inPoint ) / std::max( 0.01f, tc.clip.speed ) );
					}
					ImGui::Separator();
					if( ImGui::MenuItem( "Delete", "Del" ) )
						DeleteSelection( app );
					ImGui::EndPopup();
				}
				ImGui::PopID();
			}
		}
	}

	// A clip being dragged: trims follow the mouse at once; a move shows where it will land.
	if( state.drag == Drag::TrimIn || state.drag == Drag::TrimOut || state.drag == Drag::Move )
	{
		bool alive = state.dragTrack < (int)t.tracks.size() && state.dragIndex < (int)t.tracks[ state.dragTrack ].clips.size();
		if( alive )
		{
			TimelineTrack& track = t.tracks[ state.dragTrack ];
			TimelineClip& tc     = track.clips[ state.dragIndex ];
			double prevEnd       = state.dragIndex > 0 ? track.clips[ state.dragIndex - 1 ].End() : 0.0;
			double nextStart     = state.dragIndex + 1 < (int)track.clips.size() ? track.clips[ state.dragIndex + 1 ].start : 1e12;
			double speed         = std::max( 0.01f, tc.clip.speed );
			double mouse         = timeAt( io.MousePos.x );
			if( state.drag == Drag::TrimIn )
			{
				double end   = state.origStart + state.origLength;
				double start = Snapped( t, mouse, freeDrag );
				// Not before the media's start, a neighbour, or past the end.
				start = std::max( { start, prevEnd, state.origStart - state.origIn / speed } );
				start = std::min( start, end - 0.05 );
				tc.start        = start;
				tc.length       = end - start;
				tc.clip.inPoint = std::max( 0.0, state.origIn + ( start - state.origStart ) * speed );
			}
			else if( state.drag == Drag::TrimOut )
			{
				double end = std::min( Snapped( t, mouse, freeDrag ), nextStart );
				tc.length  = std::max( 0.05, end - tc.start );
			}
			else
			{
				state.targetStart = Snapped( t, mouse - state.grab, freeDrag );
				int over          = trackAt( io.MousePos.y );
				if( over >= 0 && t.tracks[ over ].audio == track.audio )
					state.targetTrack = over;
				// The ghost.
				if( state.targetTrack >= 0 )
				{
					ImVec2 a( X( state.targetStart ), trackY[ state.targetTrack ] + Ui( 2 ) );
					ImVec2 b( X( state.targetStart + tc.length ), trackY[ state.targetTrack ] + trackH[ state.targetTrack ] - Ui( 3 ) );
					draw->PushClipRect( ImVec2( laneLeft, laneTop ), ImVec2( viewRight, viewBottom ), true );
					draw->AddRectFilled( a, b, IM_COL32( 102, 225, 192, 50 ), Ui( 3 ) );
					draw->AddRect( a, b, theme::Accent, Ui( 3 ), 0, Ui( 2 ) );
					draw->PopClipRect();
					ImGui::SetTooltip( "%s%s at %s", state.copy ? "Copy " : "", tc.clip.name.c_str(), t.BarsBeats( state.targetStart ).c_str() );
				}
			}
		}
		if( !ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
		{
			// Released: a move or copy lands (overwriting what it covers).
			if( alive && state.drag == Drag::Move && state.targetTrack >= 0
			    && ( state.targetTrack != state.dragTrack || std::fabs( state.targetStart - state.origStart ) > 1e-6 ) )
			{
				std::vector< TimelineClip >& from = t.tracks[ state.dragTrack ].clips;
				TimelineClip moved;
				if( state.copy )
				{
					moved.clip   = CopyClip( from[ state.dragIndex ].clip );
					moved.length = from[ state.dragIndex ].length;
				}
				else
				{
					moved = std::move( from[ state.dragIndex ] );
					from.erase( from.begin() + state.dragIndex );
				}
				moved.start = state.targetStart;
				int placed  = t.Place( state.targetTrack, std::move( moved ) );
				SelectClip( state.targetTrack, placed );
			}
			state.drag = Drag::None;
		}
	}

	// Drops: deck clips (from the Clips panel), files (Files panel or the desktop), onto a track at the mouse.
	ImRect lanes( ImVec2( laneLeft, laneTop ), ImVec2( viewRight, viewBottom ) );
	if( ImGui::BeginDragDropTargetCustom( lanes, ImGui::GetID( "##lanesDrop" ) ) )
	{
		int over      = trackAt( io.MousePos.y );
		double at     = Snapped( t, timeAt( io.MousePos.x ), freeDrag );
		if( over >= 0 )
		{
			draw->AddLine( ImVec2( X( at ), trackY[ over ] ), ImVec2( X( at ), trackY[ over ] + trackH[ over ] ), theme::Accent, Ui( 2 ) );
			if( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( kCellPayload ) )
			{
				CellRef ref = *(const CellRef*)payload->Data;
				if( Clip* source = app.composition.ClipAt( ref.layer, ref.column ); source && !source->Empty() )
					SelectClip( over, app.AddTimelineClip( over, at, CopyClip( *source ) ) );
			}
			if( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( kFilePayload ) )
				SelectClip( over, app.AddTimelineFile( over, at, std::string( (const char*)payload->Data ) ) );
		}
		ImGui::EndDragDropTarget();
	}
	if( !app.droppedPaths.empty() && lanes.Contains( ImVec2( app.dropX, app.dropY ) ) )
	{
		int over  = trackAt( app.dropY );
		double at = Snapped( t, timeAt( app.dropX ), false );
		if( over >= 0 )
			for( const std::string& path : app.droppedPaths )
			{
				int placed = app.AddTimelineFile( over, at, path );
				if( placed >= 0 )
					at = t.tracks[ over ].clips[ placed ].End();
			}
		app.droppedPaths.clear(); // taken: not for the deck
	}

	// Track headers, at the left whatever the scroll.
	// (ImGui's clip rect: the items' too, not only the drawing.)
	ImGui::PushClipRect( ImVec2( stickyX, laneTop ), ImVec2( stickyX + headerW, viewBottom ), true );
	for( int k = (int)t.tracks.size() - 1; k >= 0; --k )
		if( k < (int)t.tracks.size() && trackY[ k ] + trackH[ k ] > laneTop && trackY[ k ] < viewBottom )
			TrackHeader( app, k, ImVec2( stickyX, trackY[ k ] ), ImVec2( headerW - Ui( 4 ), trackH[ std::min( k, (int)trackH.size() - 1 ) ] ) );
	ImGui::PopClipRect();
	if( t.tracks.empty() )
	{
		ImGui::SetCursorScreenPos( ImVec2( stickyX + Ui( 10 ), laneTop + Ui( 10 ) ) );
		ImGui::TextDisabled( "No tracks: add a video or audio track above, then drag clips from the Clips panel or files from Files." );
	}

	// The ruler: bars (and timecode under them when zoomed in); click or drag to move the playhead.
	draw->AddRectFilled( ImVec2( laneLeft, stickyY ), ImVec2( viewRight, stickyY + rulerH ), theme::Raised );
	{
		double bar  = t.SecondsPerBar();
		double step = bar;
		while( step * pps < Ui( 46 ) )
			step *= 2;
		double first = std::floor( timeAt( laneLeft ) / step ) * step;
		draw->PushClipRect( ImVec2( laneLeft, stickyY ), ImVec2( viewRight, stickyY + rulerH ), true );
		for( double time = first; X( time ) < viewRight; time += step )
		{
			float x = X( time );
			draw->AddLine( ImVec2( x, stickyY + rulerH * 0.45f ), ImVec2( x, stickyY + rulerH ), theme::TextDim );
			char label[ 32 ];
			std::snprintf( label, sizeof( label ), "%d", (int)std::llround( time / bar ) + 1 );
			draw->AddText( Fonts().mono, ImGui::GetFontSize() * 0.8f, ImVec2( x + Ui( 3 ), stickyY + Ui( 2 ) ), theme::TextDim, label );
		}
		draw->PopClipRect();
	}
	ImGui::SetCursorScreenPos( ImVec2( laneLeft, stickyY ) );
	ImGui::InvisibleButton( "##ruler", ImVec2( std::max( 1.0f, viewRight - laneLeft ), rulerH ) );
	if( ImGui::IsItemActive() )
	{
		t.Locate( Snapped( t, timeAt( io.MousePos.x ), true ) );
		state.drag = Drag::Scrub;
	}
	else if( state.drag == Drag::Scrub )
		state.drag = Drag::None;
	if( ImGui::IsItemHovered() && !app.learning )
		ImGui::SetTooltip( "%s  %s\nClick or drag: move the playhead", t.BarsBeats( timeAt( io.MousePos.x ) ).c_str(), t.Timecode( timeAt( io.MousePos.x ) ).c_str() );

	// The block lane: each block a coloured region with its name and end action. Drag a free part to draw a new
	// block; drag a block to move it, its edges to resize it.
	float laneY = stickyY + rulerH;
	draw->AddRectFilled( ImVec2( laneLeft, laneY ), ImVec2( viewRight, laneY + blockH ), theme::Panel );
	ImGui::SetCursorScreenPos( ImVec2( laneLeft, laneY ) );
	ImGui::SetNextItemAllowOverlap(); // the blocks on it come first
	ImGui::InvisibleButton( "##blockLane", ImVec2( std::max( 1.0f, viewRight - laneLeft ), blockH ) );
	if( ImGui::IsItemActivated() )
	{
		state.drag     = Drag::BlockNew;
		state.newStart = state.newEnd = Snapped( t, timeAt( io.MousePos.x ), freeDrag );
	}
	if( ImGui::IsItemHovered() && state.drag == Drag::None && !app.learning )
		ImGui::SetTooltip( "Drag here to make a block (a song); each block is a cue" );
	ImGui::PushClipRect( ImVec2( laneLeft, laneY ), ImVec2( viewRight, laneY + blockH ), true );
	for( int i = 0; i < (int)t.blocks.size(); ++i )
	{
		TimelineBlock& bk = t.blocks[ i ];
		ImVec2 a( X( bk.start ), laneY + Ui( 2 ) ), b( X( bk.end ), laneY + blockH - Ui( 2 ) );
		if( b.x < laneLeft || a.x > viewRight )
			continue;
		bool selected = state.block == i;
		draw->AddRectFilled( a, b, BlockColour( bk.colour ), Ui( 4 ) );
		if( selected )
			draw->AddRect( a, b, theme::Selected, Ui( 4 ), 0, Ui( 2 ) );
		char label[ 160 ];
		std::snprintf( label, sizeof( label ), "%d %s  > %s", i + 1, bk.name.c_str(), BlockEndName( bk.endAction ) );
		draw->PushClipRect( ImVec2( std::max( a.x, laneLeft ), a.y ), ImVec2( std::min( b.x, viewRight ), b.y ), true );
		draw->AddText( ImVec2( std::max( a.x, laneLeft ) + Ui( 5 ), a.y + ( b.y - a.y - ImGui::GetTextLineHeight() ) / 2 ), theme::Text, label );
		draw->PopClipRect();
		ImGui::PushID( 900000 + i );
		ImVec2 va( std::max( a.x, laneLeft ), a.y );
		ImGui::SetCursorScreenPos( va );
		ImGui::InvisibleButton( "##block", ImVec2( std::max( 1.0f, std::min( b.x, viewRight ) - va.x ), b.y - a.y ) );
		float mx  = io.MousePos.x;
		bool atIn = mx - a.x < edge && b.x - a.x > edge * 3, atOut = b.x - mx < edge && b.x - a.x > edge * 3;
		if( ImGui::IsItemHovered() && ( atIn || atOut ) && state.drag == Drag::None )
			ImGui::SetMouseCursor( ImGuiMouseCursor_ResizeEW );
		if( ImGui::IsItemActivated() )
		{
			state.block      = i;
			state.clip       = -1;
			state.drag       = atIn ? Drag::BlockStart : atOut ? Drag::BlockEnd : Drag::BlockMove;
			state.dragIndex  = i;
			state.grab       = timeAt( mx ) - bk.start;
			state.origStart  = bk.start;
			state.origLength = bk.end - bk.start;
		}
		if( ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
			app.TimelineCue( i );
		if( ImGui::IsItemHovered() && state.drag == Drag::None && !app.learning && ImGui::IsItemHovered( ImGuiHoveredFlags_DelayNormal ) )
			ImGui::SetTooltip( "Cue %d: %s (%s - %s), then %s\nDouble-click: play it; drag: move; edges: resize; right-click: name, end, colour", i + 1,
			                   bk.name.c_str(), t.BarsBeats( bk.start ).c_str(), t.BarsBeats( bk.end ).c_str(), BlockEndName( bk.endAction ) );
		if( ImGui::BeginPopupContextItem( "##blockMenu" ) )
		{
			state.block = i;
			ImGui::SetNextItemWidth( Ui( 180 ) );
			ImGui::InputText( "Name", &bk.name );
			ImGui::TextDisabled( "When it ends" );
			for( int e = 0; e < (int)BlockEnd::Count; ++e )
				if( ImGui::RadioButton( BlockEndName( (BlockEnd)e ), bk.endAction == (BlockEnd)e ) )
					bk.endAction = (BlockEnd)e;
			ImGui::TextDisabled( "Colour" );
			for( int c = 0; c < kColourCount; ++c )
			{
				if( c > 0 )
					ImGui::SameLine( 0, Ui( 3 ) );
				ImGui::PushID( c );
				ImGui::PushStyleColor( ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4( BlockColour( c ) ) );
				if( ImGui::Button( bk.colour == c ? "o" : " ", ImVec2( Ui( 18 ), Ui( 18 ) ) ) )
					bk.colour = c;
				ImGui::PopStyleColor();
				ImGui::PopID();
			}
			ImGui::Separator();
			if( ImGui::MenuItem( "Play This Cue" ) )
				app.TimelineCue( i );
			if( ImGui::MenuItem( "Delete Block" ) )
			{
				t.blocks.erase( t.blocks.begin() + i );
				state.block = -1;
				ImGui::EndPopup();
				ImGui::PopID();
				break;
			}
			ImGui::EndPopup();
		}
		ImGui::PopID();
	}
	ImGui::PopClipRect();
	// Block drags, kept clear of the neighbours.
	if( state.drag == Drag::BlockMove || state.drag == Drag::BlockStart || state.drag == Drag::BlockEnd )
	{
		if( state.dragIndex < (int)t.blocks.size() )
		{
			TimelineBlock& bk = t.blocks[ state.dragIndex ];
			double prevEnd    = state.dragIndex > 0 ? t.blocks[ state.dragIndex - 1 ].end : 0.0;
			double nextStart  = state.dragIndex + 1 < (int)t.blocks.size() ? t.blocks[ state.dragIndex + 1 ].start : 1e12;
			double mouse      = timeAt( io.MousePos.x );
			if( state.drag == Drag::BlockMove )
			{
				double start = Snapped( t, mouse - state.grab, freeDrag );
				start        = std::clamp( start, prevEnd, std::max( prevEnd, nextStart - state.origLength ) );
				bk.start     = start;
				bk.end       = start + state.origLength;
			}
			else if( state.drag == Drag::BlockStart )
				bk.start = std::clamp( Snapped( t, mouse, freeDrag ), prevEnd, bk.end - 0.05 );
			else
				bk.end = std::clamp( Snapped( t, mouse, freeDrag ), bk.start + 0.05, nextStart );
		}
		if( !ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
			state.drag = Drag::None;
	}
	if( state.drag == Drag::BlockNew )
	{
		state.newEnd = Snapped( t, timeAt( io.MousePos.x ), freeDrag );
		double s = std::min( state.newStart, state.newEnd ), e = std::max( state.newStart, state.newEnd );
		// Not over another block.
		for( const TimelineBlock& bk : t.blocks )
		{
			if( bk.end <= state.newStart && bk.end > s )
				s = bk.end;
			if( bk.start >= state.newStart && bk.start < e )
				e = bk.start;
		}
		draw->AddRectFilled( ImVec2( X( s ), laneY + Ui( 2 ) ), ImVec2( X( e ), laneY + blockH - Ui( 2 ) ), IM_COL32( 255, 255, 255, 50 ), Ui( 4 ) );
		if( !ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
		{
			state.drag = Drag::None;
			if( e - s > 0.05 && t.BlockAt( s ) < 0 )
			{
				TimelineBlock block;
				block.name   = "Song " + std::to_string( t.blocks.size() + 1 );
				block.start  = s;
				block.end    = e;
				block.colour = (int)t.blocks.size();
				t.blocks.push_back( block );
				t.SortBlocks();
				for( int i = 0; i < (int)t.blocks.size(); ++i )
					if( t.blocks[ i ].start == s )
						state.block = i;
				state.clip = -1;
			}
		}
	}

	// The corner over the headers, and the playhead across everything.
	draw->AddRectFilled( ImVec2( stickyX, stickyY ), ImVec2( laneLeft, laneTop ), theme::Panel );
	draw->AddText( Fonts().bold, ImGui::GetFontSize() * 0.85f, ImVec2( stickyX + Ui( 6 ), stickyY + Ui( 4 ) ), theme::TextDim, "BARS" );
	draw->AddText( Fonts().bold, ImGui::GetFontSize() * 0.85f, ImVec2( stickyX + Ui( 6 ), laneY + Ui( 6 ) ), theme::TextDim, "BLOCKS / CUES" );
	float px = X( t.position );
	if( px >= laneLeft && px <= viewRight )
	{
		draw->AddLine( ImVec2( px, stickyY ), ImVec2( px, std::min( viewBottom, origin.y + topH + tracksH ) ), t.black ? theme::Warning : theme::Accent, Ui( 1.5f ) );
		float s = Ui( 6 );
		draw->AddTriangleFilled( ImVec2( px - s, stickyY ), ImVec2( px + s, stickyY ), ImVec2( px, stickyY + s * 1.4f ), t.black ? theme::Warning : theme::Accent );
	}
	ImGui::EndChild();
}

// ---- the inspector: the selected clip or block

void Inspector( App& app )
{
	Timeline& t = app.composition.timeline;
	auto row    = [ & ]( const char* label ) {
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled( "%s", label );
		ImGui::SameLine( Ui( 70 ) );
		ImGui::SetNextItemWidth( -FLT_MIN );
	};
	auto timeField = [ & ]( const char* id, double& value, double min ) {
		float v = (float)value;
		if( ImGui::DragFloat( id, &v, 0.01f, (float)min, 100000.0f, "%.3f s" ) )
		{
			value = std::max( min, (double)v );
			return true;
		}
		return false;
	};
	if( TimelineClip* tc = SelectedClip( t ) )
	{
		TimelineTrack& track = t.tracks[ state.track ];
		ImGui::SeparatorText( "Clip" );
		row( "Name" );
		ImGui::InputText( "##name", &tc->clip.name );
		row( "Start" );
		double start = tc->start;
		if( timeField( "##start", start, 0 ) )
		{
			// Moved by typing: it lands like a drag (overwriting what it covers).
			TimelineClip moved = std::move( track.clips[ state.clip ] );
			track.clips.erase( track.clips.begin() + state.clip );
			moved.start = start;
			state.clip  = t.Place( state.track, std::move( moved ) );
			tc          = SelectedClip( t );
			if( !tc )
				return;
		}
		ImGui::TextDisabled( "        %s   %s", t.BarsBeats( tc->start ).c_str(), t.Timecode( tc->start ).c_str() );
		row( "Length" );
		double length = tc->length;
		if( timeField( "##length", length, 0.05 ) )
		{
			double limit = state.clip + 1 < (int)track.clips.size() ? track.clips[ state.clip + 1 ].start - tc->start : 1e12;
			tc->length   = std::min( length, limit );
		}
		ImGui::TextDisabled( "        %.2f bars", tc->length / t.SecondsPerBar() );
		row( "Media in" );
		timeField( "##in", tc->clip.inPoint, 0 );
		row( "Speed" );
		FilledSlider( "##speed", &tc->clip.speed, 0.0f, 4.0f, "%.2fx" );
		ResetValue( &tc->clip.speed, 1.0f );
		row( "Past end" );
		int mode = tc->clip.playMode == PlayMode::Loop ? 0 : 1;
		if( ImGui::Combo( "##pastEnd", &mode, "Loop the media\0Hold the last frame\0" ) )
			tc->clip.playMode = mode == 0 ? PlayMode::Loop : PlayMode::Once;
		if( !track.audio )
		{
			row( "Opacity" );
			float percent = tc->clip.opacity * 100;
			if( FilledSlider( "##opacity", &percent, 0, 100, "%.0f %%" ) )
				tc->clip.opacity = percent / 100;
			ResetValue( &tc->clip.opacity, 1.0f );
		}
		if( tc->clip.audio )
		{
			row( "Volume" );
			float percent = tc->clip.volume * 100;
			if( FilledSlider( "##volume", &percent, 0, 100, "%.0f %%" ) )
				tc->clip.volume = percent / 100;
			ResetValue( &tc->clip.volume, 1.0f );
		}
		ImGui::Spacing();
		float half = ( ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x ) / 2;
		if( ImGui::Button( "Duplicate", ImVec2( half, 0 ) ) )
			Duplicate( app, state.track, state.clip );
		ImGui::SameLine();
		if( ImGui::Button( "Delete", ImVec2( half, 0 ) ) )
			DeleteSelection( app );
		if( ImGui::Button( "Split at Playhead", ImVec2( -FLT_MIN, 0 ) ) )
			SplitAtPlayhead( app );
		ImGui::TextDisabled( "%s", tc->clip.path.empty() ? tc->clip.generatorId.c_str() : tc->clip.path.c_str() );
		if( !tc->clip.Loaded() && !tc->clip.generator && !tc->clip.audio )
			ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( theme::Error ), "Media not found or not supported" );
		return;
	}
	if( state.block >= 0 && state.block < (int)t.blocks.size() )
	{
		TimelineBlock& bk = t.blocks[ state.block ];
		ImGui::SeparatorText( ( "Block - cue " + std::to_string( state.block + 1 ) ).c_str() );
		row( "Name" );
		ImGui::InputText( "##blockName", &bk.name );
		row( "Start" );
		double prevEnd   = state.block > 0 ? t.blocks[ state.block - 1 ].end : 0.0;
		double nextStart = state.block + 1 < (int)t.blocks.size() ? t.blocks[ state.block + 1 ].start : 1e12;
		double start     = bk.start;
		if( timeField( "##blockStart", start, prevEnd ) )
			bk.start = std::min( start, bk.end - 0.05 );
		ImGui::TextDisabled( "        %s   %s", t.BarsBeats( bk.start ).c_str(), t.Timecode( bk.start ).c_str() );
		row( "End" );
		double end = bk.end;
		if( timeField( "##blockEnd", end, bk.start + 0.05 ) )
			bk.end = std::min( end, nextStart );
		ImGui::TextDisabled( "        %s   %s", t.BarsBeats( bk.end ).c_str(), t.Timecode( bk.end ).c_str() );
		ImGui::TextDisabled( "When it ends" );
		const char* help[] = { "Next: plays on into what follows", "Loop: starts the block again until another cue", "Hold: stops on the last frame",
			                   "Black: stops, and the output goes black" };
		for( int e = 0; e < (int)BlockEnd::Count; ++e )
		{
			if( ImGui::RadioButton( help[ e ], bk.endAction == (BlockEnd)e ) )
				bk.endAction = (BlockEnd)e;
		}
		ImGui::Spacing();
		if( ImGui::Button( "Play This Cue", ImVec2( -FLT_MIN, 0 ) ) )
			app.TimelineCue( state.block );
		if( ImGui::Button( "Delete Block", ImVec2( -FLT_MIN, 0 ) ) )
			DeleteSelection( app );
		return;
	}
	ImGui::TextDisabled( "Select a clip or a block." );
	ImGui::Spacing();
	ImGui::PushTextWrapPos( 0 );
	ImGui::TextDisabled( "Drag clips from the Clips panel, or files from Files or the desktop, onto a track. Blocks are the songs: a cue starts "
	                     "each one, and its end action says what follows: Next, Loop, Hold or Black." );
	ImGui::PopTextWrapPos();
}
}

void DrawTimelinePanel( App& app )
{
	if( !ImGui::Begin( panel::Timeline, &app.panels.timeline ) )
	{
		ImGui::End();
		return;
	}
	Timeline& t = app.composition.timeline;
	// Selection that no longer exists (undo, a deleted track).
	if( !SelectedClip( t ) )
		state.clip = -1;
	if( state.block >= (int)t.blocks.size() )
		state.block = -1;

	Toolbar( app );
	CueStrip( app );

	// Keys while the timeline has the focus.
	const ImGuiIO& io = ImGui::GetIO();
	if( ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) && !io.WantTextInput && !app.learning )
	{
		if( ImGui::IsKeyPressed( ImGuiKey_Delete, false ) || ImGui::IsKeyPressed( ImGuiKey_Backspace, false ) )
			DeleteSelection( app );
		if( io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_D, false ) && SelectedClip( t ) )
			Duplicate( app, state.track, state.clip );
		if( io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_E, false ) )
			SplitAtPlayhead( app );
	}

	float inspectorW = Ui( 250 );
	float height     = ImGui::GetContentRegionAvail().y;
	ImGui::BeginChild( "##main", ImVec2( ImGui::GetContentRegionAvail().x - inspectorW - ImGui::GetStyle().ItemSpacing.x, height ) );
	Lanes( app, height );
	ImGui::EndChild();
	ImGui::SameLine();
	ImGui::BeginChild( "##inspector", ImVec2( 0, height ), ImGuiChildFlags_Border );
	Inspector( app );
	ImGui::EndChild();
	ImGui::End();
}
}
