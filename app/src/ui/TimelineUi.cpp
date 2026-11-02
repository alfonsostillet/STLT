#include "ui/TimelineUi.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_stdlib.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include "App.h"
#include "ui/AdvancedOutputUi.h"
#include "ui/ControlUi.h"
#include "ui/EffectsUi.h"
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
const ImU32 kBlockColours[] = { IM_COL32( 62, 104, 150, 255 ), IM_COL32( 160, 92, 40, 255 ), IM_COL32( 84, 92, 170, 255 ), IM_COL32( 150, 60, 110, 255 ),
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
	Key,        // a keyframe (every one at that moment) along a clip
	VolumeKey,  // a point of an audio track's volume line
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
	std::vector< Keyframe > origKeys[ (int)KeyProperty::Count ]; // a clip's keyframes when a trim began
	std::vector< ParamKeys > origParamKeys;
	double keyFrom = 0; // a keyframe being dragged on a clip: where it was (from the clip's start)
	bool keyMoved  = false;
	float menuVolume = 1; // the volume at the right-click on a volume line
	bool copy       = false;
	int targetTrack = -1;
	double targetStart = 0;
	double newStart    = 0, newEnd = 0;
	float scrollX      = -1; // a scroll to apply (-1: none)
	float headerWidth  = 250; // the track headers' column (at 15 px type): drag its edge
	int renaming       = -1;  // the track whose name is being typed (double-click its header)
	bool renameFocus   = false;
	int renameWait     = 0; // frames the field gets to take the keyboard before it counts as left
} state;

// The tracks top to bottom as shown, as in an editing program: the video tracks (the top one, drawn over the others,
// first) and under them the audio tracks.
std::vector< int > ShownOrder( const Timeline& t )
{
	std::vector< int > order;
	for( int k = (int)t.tracks.size() - 1; k >= 0; --k )
		if( !t.tracks[ k ].audio )
			order.push_back( k );
	for( int k = 0; k < (int)t.tracks.size(); ++k )
		if( t.tracks[ k ].audio )
			order.push_back( k );
	return order;
}

// Moves a track to another's place (video among video, audio among audio): the order video tracks lie in.
void MoveTrack( App& app, int from, int to )
{
	std::vector< TimelineTrack >& tracks = app.composition.timeline.tracks;
	if( from == to || from < 0 || to < 0 || from >= (int)tracks.size() || to >= (int)tracks.size() || tracks[ from ].audio != tracks[ to ].audio )
		return;
	TimelineTrack moved = std::move( tracks[ from ] );
	tracks.erase( tracks.begin() + from );
	tracks.insert( tracks.begin() + to, std::move( moved ) );
	auto follow = [ & ]( int& index ) {
		if( index == from )
			index = to;
		else if( from < index && index <= to )
			--index;
		else if( to <= index && index < from )
			++index;
	};
	follow( state.track );
	follow( state.renaming );
	follow( state.dragTrack );
}

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
	copy.CopyLookFrom( from );
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

	// TC: the playhead follows the timecode coming in (the source is in Preferences > Sync), the timeline's 0
	// being the timecode in the field next to it. Lit while it follows; amber while it waits for the timecode.
	ImGui::SameLine( 0, Ui( 14 ) );
	{
		bool on       = t.followTimecode;
		bool locked   = on && app.timecodeLocked;
		bool noSource = app.timecode.Current() == TimecodeInput::Source::Off;
		ImU32 face    = locked ? theme::AccentDim : on ? IM_COL32( 110, 78, 24, 255 ) : theme::Raised;
		ImGui::PushStyleColor( ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4( face ) );
		ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4( locked ? theme::AccentHover : theme::RaisedHover ) );
		ImGui::PushFont( Fonts().bold );
		if( ImGui::Button( " TC " ) )
			t.followTimecode = !on;
		ImGui::PopFont();
		ImGui::PopStyleColor( 2 );
		if( ImGui::IsItemHovered() )
		{
			std::string status = app.timecode.Status();
			ImGui::SetTooltip( "Follow the timecode: the playhead goes where the timecode coming in says, and plays and "
			                   "stops with it.\n%s",
			                   noSource ? "Choose its source (LTC, MTC or Art-Net) in Preferences > Sync." : status.c_str() );
		}
		if( on )
		{
			ImGui::SameLine( 0, Ui( 4 ) );
			ImGui::PushFont( Fonts().mono );
			ImGui::SetNextItemWidth( Ui( 92 ) );
			static std::string start; // what is being typed, kept until the field is left
			static bool typing = false;
			if( !typing )
				start = t.timecodeStart;
			ImGui::InputText( "##tcStart", &start, ImGuiInputTextFlags_CharsNoBlank );
			typing = ImGui::IsItemActive();
			if( ImGui::IsItemDeactivatedAfterEdit() )
			{
				Timecode parsed;
				if( ParseTimecode( start, parsed ) )
					t.timecodeStart = parsed.Text(); // anything else: the start stays as it was
			}
			ImGui::PopFont();
			if( ImGui::IsItemHovered() )
				ImGui::SetTooltip( "The timecode at the timeline's start (shows often begin at 01:00:00:00)" );
			ImGui::SameLine( 0, Ui( 4 ) );
			TimecodeReceiver::State in = app.timecode.At( TempoSync::Now(), 0 );
			ImGui::BeginDisabled( !in.present );
			if( ImGui::Button( "Take" ) )
			{
				// The timecode coming in now becomes the start, less where the playhead is.
				double origin = in.seconds - t.position;
				double fps    = TimecodeRateFps( in.last.rate );
				int64_t frame = (int64_t)std::llround( std::max( 0.0, origin ) * fps );
				Timecode s;
				s.rate    = in.last.rate == TimecodeRate::Fps2997Drop ? TimecodeRate::Fps30 : in.last.rate; // whole frames
				int per   = TimecodeRateFrames( s.rate );
				s.frames  = (int)( frame % per );
				s.seconds = (int)( frame / per % 60 );
				s.minutes = (int)( frame / per / 60 % 60 );
				s.hours   = (int)( frame / per / 3600 % 24 );
				t.timecodeStart = s.Text();
			}
			ImGui::EndDisabled();
			if( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
				ImGui::SetTooltip( "The timecode coming in now goes where the playhead is" );
			if( locked || ( on && !noSource ) )
			{
				ImGui::SameLine( 0, Ui( 6 ) );
				ImGui::PushFont( Fonts().mono );
				ImGui::AlignTextToFramePadding();
				if( in.present )
					ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( locked ? theme::Accent : theme::TextDim ), "%s", in.last.Text().c_str() );
				else
					ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( theme::Warning ), "no timecode" );
				ImGui::PopFont();
			}
		}
	}

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

bool KeyButton( const char* id, bool onKey, bool animated, bool enabled, const char* tip );

constexpr const char* kTrackPayload = "STLT_TRACK";

void TrackHeader( App& app, int k, ImVec2 pos, ImVec2 size )
{
	Timeline& t          = app.composition.timeline;
	TimelineTrack& track = t.tracks[ k ];
	ImDrawList* draw     = ImGui::GetWindowDrawList();
	const ImU32 videoTint = IM_COL32( 74, 120, 190, 255 ), audioTint = IM_COL32( 64, 180, 104, 255 );
	// Audio tracks green, video tracks blue: a strip at the left, the background tinted.
	draw->AddRectFilled( pos, ImVec2( pos.x + size.x, pos.y + size.y - Ui( 1 ) ), track.audio ? IM_COL32( 30, 54, 38, 255 ) : theme::Raised );
	draw->AddRectFilled( pos, ImVec2( pos.x + Ui( 4 ), pos.y + size.y - Ui( 1 ) ), track.audio ? audioTint : videoTint );
	ImGui::PushID( k );
	float left = pos.x + Ui( 10 ), right = pos.x + size.x - Ui( 6 ), row = ImGui::GetFrameHeight() + Ui( 2 );

	// First row: the name (double-click the header to rename it), then Vol, M and S at the right.
	float buttonsW = Ui( track.audio ? 82 : 46 );
	ImGui::SetCursorScreenPos( ImVec2( right - buttonsW, pos.y + Ui( 3 ) ) );
	if( track.audio )
	{
		// The volume line over the track: open to draw and edit it.
		if( SmallToggle( "Vol", track.volumeLine, IM_COL32( 245, 200, 90, 255 ),
		                 "Volume line: draw the volume over the track with keyframes (double-click the track to add one, drag to move, "
		                 "right-click for curve or delete); clips stay put while it is open" ) )
			track.volumeLine = !track.volumeLine;
		ImGui::SameLine( 0, Ui( 4 ) );
	}
	if( SmallToggle( "M", track.mute, theme::Warning, "Mute: the track off" ) )
		track.mute = !track.mute;
	ImGui::SameLine( 0, Ui( 2 ) );
	if( SmallToggle( "S", track.solo, theme::Solo, "Solo: only this track (and other soloed ones)" ) )
		track.solo = !track.solo;
	float nameRight = right - buttonsW - Ui( 6 );
	if( state.renaming == k )
	{
		ImGui::SetCursorScreenPos( ImVec2( left - Ui( 4 ), pos.y + Ui( 2 ) ) );
		ImGui::SetNextItemWidth( std::max( Ui( 40 ), nameRight - left + Ui( 4 ) ) );
		if( state.renameFocus )
		{
			ImGui::SetKeyboardFocusHere();
			state.renameFocus = false;
			state.renameWait  = 2;
		}
		ImGui::InputText( "##rename", &track.name, ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue );
		// Done when the field is left (Enter, a click elsewhere, Escape), or when it lost the keyboard while out of
		// sight (scrolled away) and so never got to be left.
		if( state.renameWait > 0 )
			--state.renameWait;
		if( ImGui::IsItemDeactivated() || ImGui::IsKeyPressed( ImGuiKey_Escape ) || ( state.renameWait == 0 && !ImGui::IsItemActive() ) )
			state.renaming = -1;
	}
	else
	{
		draw->PushClipRect( ImVec2( left, pos.y ), ImVec2( std::max( left, nameRight ), pos.y + row + Ui( 2 ) ), true );
		draw->AddText( Fonts().bold, ImGui::GetFontSize(), ImVec2( left, pos.y + Ui( 5 ) ), theme::Text, track.name.c_str() );
		draw->PopClipRect();
	}

	// Second row (video): where the picture goes, a chip in that screen's colour; click to choose.
	float y2 = pos.y + row + Ui( 3 );
	if( !track.audio )
	{
		AdvancedOutputSetup& setup = app.timelineOutput;
		// A screen not in the setup (removed, or a setup not saved): every screen shows the track.
		std::string name = "\xe2\x86\x92 " + PictureName( app, track.output );
		ImVec2 at( left, y2 );
		float width = std::max( Ui( 30 ), right - left );
		float h     = ImGui::GetFrameHeight();
		ImGui::SetCursorScreenPos( at );
		ImGui::InvisibleButton( "##output", ImVec2( width, h ) );
		bool hot     = ImGui::IsItemHovered();
		ImU32 colour = PictureColour( app, track.output );
		draw->AddRectFilled( at, ImVec2( at.x + width, at.y + h ), hot ? theme::RaisedHover : theme::Well, Ui( 4 ) );
		draw->AddRectFilled( ImVec2( at.x + Ui( 6 ), at.y + h / 2 - Ui( 4 ) ), ImVec2( at.x + Ui( 14 ), at.y + h / 2 + Ui( 4 ) ), colour, Ui( 2 ) );
		draw->PushClipRect( at, ImVec2( at.x + width - Ui( 16 ), at.y + h ), true );
		draw->AddText( ImVec2( at.x + Ui( 20 ), at.y + ( h - ImGui::GetFontSize() ) / 2 ), theme::Text, name.c_str() );
		draw->PopClipRect();
		draw->AddTriangleFilled( ImVec2( at.x + width - Ui( 12 ), at.y + h / 2 - Ui( 2 ) ), ImVec2( at.x + width - Ui( 4 ), at.y + h / 2 - Ui( 2 ) ),
		                         ImVec2( at.x + width - Ui( 8 ), at.y + h / 2 + Ui( 3 ) ), theme::TextDim );
		if( hot )
			ImGui::SetTooltip( "Goes to: %s\nClick to send this track to one screen, or to all of them (the composition).\nThe screens are those "
			                   "of the Advanced Output's Timeline setup.",
			                   PictureName( app, track.output ).c_str() );
		if( ImGui::IsItemClicked( ImGuiMouseButton_Left ) )
			ImGui::OpenPopup( "##outputMenu" );
		if( ImGui::BeginPopup( "##outputMenu" ) )
		{
			int current = TrackScreen( app, k );
			auto item   = [ & ]( int screen, const std::string& label, bool selected ) {
                ImVec2 p = ImGui::GetCursorScreenPos();
                ImGui::GetWindowDrawList()->AddRectFilled( ImVec2( p.x, p.y + Ui( 4 ) ), ImVec2( p.x + Ui( 8 ), p.y + Ui( 12 ) ),
                                                           screen >= 0 ? ScreenColour( screen ) : theme::Accent, Ui( 2 ) );
                ImGui::SetCursorScreenPos( ImVec2( p.x + Ui( 14 ), p.y ) );
                if( ImGui::Selectable( label.c_str(), selected ) )
                    SendTrackTo( app, k, screen );
			};
			ImGui::TextDisabled( "Send %s to", track.name.c_str() );
			char text[ 200 ];
			std::snprintf( text, sizeof( text ), "All Screens (composition %dx%d)", app.composition.width, app.composition.height );
			item( -1, text, !setup.FindCanvas( track.output ) );
			for( size_t i = 0; i < setup.screens.size(); ++i )
			{
				std::snprintf( text, sizeof( text ), "%s  %dx%d##screen%d", setup.screens[ i ].name.c_str(), setup.screens[ i ].width, setup.screens[ i ].height, (int)i );
				item( (int)i, text, current == (int)i );
			}
			ImGui::Separator();
			if( ImGui::Selectable( "Screens: Advanced Output..." ) )
			{
				app.outputTab             = 1;
				app.panels.advancedOutput = true;
			}
			ImGui::EndPopup();
		}
		y2 += ImGui::GetFrameHeight() + Ui( 3 );
	}

	// Last row: opacity, or the volume (with keyframes the slider shows the volume at the playhead, and an edit
	// sets the keyframe there; the diamond adds or removes the keyframe at the playhead).
	if( y2 + ImGui::GetFrameHeight() <= pos.y + size.y )
	{
		ImGui::SetCursorScreenPos( ImVec2( left, y2 ) );
		if( track.audio )
		{
			double now    = t.position;
			bool keyed    = !track.volumeKeys.empty();
			bool onKey    = keyed && KeyIndex( track.volumeKeys, now ) >= 0;
			float current = track.VolumeAt( now );
			if( KeyButton( "##volKey", onKey, keyed, true,
			               onKey ? "Remove the volume keyframe at the playhead"
			               : keyed ? "Add a volume keyframe at the playhead"
			                       : "Animate the volume: a keyframe at the playhead (then move the playhead and change the volume)" ) )
			{
				if( onKey )
					track.volumeKeys.erase( track.volumeKeys.begin() + KeyIndex( track.volumeKeys, now ) );
				else
				{
					SetKeyIn( track.volumeKeys, now, current );
					track.volumeLine = true;
				}
			}
			ImGui::SameLine( 0, Ui( 4 ) );
			ImGui::SetNextItemWidth( std::max( Ui( 30 ), right - ImGui::GetCursorScreenPos().x ) );
			float percent = current * 100;
			bool edited   = FilledSlider( "##volume", &percent, 0, 100, keyed ? "Volume %.0f %% (keys)" : "Volume %.0f %%" );
			// One drag, one point: where the playhead was when it began (the timeline may play on meanwhile).
			static double heldAt = 0;
			if( ImGui::IsItemActivated() )
				heldAt = now;
			if( edited )
			{
				if( keyed )
					SetKeyIn( track.volumeKeys, ImGui::IsItemActive() ? heldAt : now, percent / 100 );
				else
					track.volume = percent / 100;
			}
			if( !keyed )
				ResetValue( &track.volume, 1.0f );
		}
		else
		{
			ImGui::SetNextItemWidth( right - left );
			float percent = track.opacity * 100;
			if( FilledSlider( "##opacity", &percent, 0, 100, "Opacity %.0f %%" ) )
				track.opacity = percent / 100;
			ResetValue( &track.opacity, 1.0f );
		}
	}

	// The free part of the header: double-click to rename, drag onto another track to move it there, right-click
	// for the track's menu.
	ImGui::SetCursorScreenPos( pos );
	ImGui::SetNextItemAllowOverlap();
	ImGui::InvisibleButton( "##header", ImVec2( size.x, size.y - Ui( 1 ) ) );
	if( ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
	{
		state.renaming    = k;
		state.renameFocus = true;
	}
	if( ImGui::IsItemHovered() && state.renaming != k && !ImGui::IsPopupOpen( "##trackMenu" ) && ImGui::IsItemHovered( ImGuiHoveredFlags_DelayNormal ) && !app.learning )
		ImGui::SetTooltip( "%s\nDouble-click: rename. Drag onto another %s track: move it there. Right-click: menu", track.name.c_str(), track.audio ? "audio" : "video" );
	if( ImGui::BeginDragDropSource( ImGuiDragDropFlags_SourceNoPreviewTooltip ) )
	{
		ImGui::SetDragDropPayload( kTrackPayload, &k, sizeof( int ) );
		draw->AddRect( pos, ImVec2( pos.x + size.x, pos.y + size.y - Ui( 1 ) ), theme::Accent, 0, 0, Ui( 2 ) );
		ImGui::EndDragDropSource();
	}
	if( ImGui::BeginDragDropTarget() )
	{
		const ImGuiPayload* payload = ImGui::GetDragDropPayload();
		int from = payload && payload->IsDataType( kTrackPayload ) ? *(const int*)payload->Data : -1;
		if( from >= 0 && from < (int)t.tracks.size() && t.tracks[ from ].audio == track.audio && from != k )
		{
			// Where it lands: a line above or below this header (video tracks above are higher in the list).
			bool above = track.audio ? from > k : from < k;
			float ly   = above ? pos.y + Ui( 1 ) : pos.y + size.y - Ui( 2 );
			draw->AddLine( ImVec2( pos.x, ly ), ImVec2( pos.x + size.x, ly ), theme::Accent, Ui( 3 ) );
			if( ImGui::AcceptDragDropPayload( kTrackPayload, ImGuiDragDropFlags_AcceptNoDrawDefaultRect ) )
			{
				MoveTrack( app, from, k );
				ImGui::EndDragDropTarget();
				ImGui::PopID();
				return;
			}
		}
		ImGui::EndDragDropTarget();
	}
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
		// Up and down as shown: video tracks above lie later in the list.
		std::vector< int > order = ShownOrder( t );
		int place = (int)( std::find( order.begin(), order.end(), k ) - order.begin() );
		int above = place > 0 && t.tracks[ order[ place - 1 ] ].audio == track.audio ? order[ place - 1 ] : -1;
		int below = place + 1 < (int)order.size() && t.tracks[ order[ place + 1 ] ].audio == track.audio ? order[ place + 1 ] : -1;
		if( ImGui::MenuItem( "Move Up", nullptr, false, above >= 0 ) )
			MoveTrack( app, k, above );
		if( ImGui::MenuItem( "Move Down", nullptr, false, below >= 0 ) )
			MoveTrack( app, k, below );
		ImGui::Separator();
		if( ImGui::MenuItem( "Delete Track" ) )
		{
			t.tracks.erase( t.tracks.begin() + k );
			state.clip = state.track = -1;
			state.renaming           = -1;
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
	const float headerW = Ui( state.headerWidth ), rulerH = Ui( 22 ), blockH = Ui( 26 ), topH = rulerH + blockH;
	const float row = ImGui::GetFrameHeight() + Ui( 3 );
	const float videoH = std::max( Ui( 66 ), row * 3 + Ui( 6 ) ), audioH = std::max( Ui( 46 ), row * 2 + Ui( 6 ) ), edge = Ui( 6 );
	const float pps = state.zoom * ImGui::GetFontSize() / 15.0f; // pixels per second
	double length   = t.Length();
	double shown    = std::max( length + 60.0, 120.0 );
	float contentW  = headerW + (float)( shown * pps );
	float tracksH   = Ui( 6 ); // the gap between the video and the audio tracks
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

	// Lanes: track backgrounds with bar lines; the video tracks over the audio ones (ShownOrder), a gap between.
	std::vector< float > trackY( t.tracks.size() ), trackH( t.tracks.size() );
	std::vector< int > order = ShownOrder( t );
	const float gapH = Ui( 6 );
	float y          = origin.y + topH;
	float splitY     = -1; // where the audio tracks begin
	for( int k : order )
	{
		if( t.tracks[ k ].audio && splitY < 0 )
		{
			splitY = y;
			if( k != order.front() )
				y += gapH;
		}
		trackY[ k ] = y;
		trackH[ k ] = t.tracks[ k ].audio ? audioH : videoH;
		y += trackH[ k ];
	}
	tracksH = y - ( origin.y + topH );
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
		                     t.tracks[ k ].audio ? IM_COL32( 18, 30, 22, 255 ) : theme::Well );
	}
	if( splitY > origin.y + topH )
		draw->AddRectFilled( ImVec2( laneLeft, splitY ), ImVec2( viewRight, splitY + gapH ), theme::Panel );
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

				if( track.audio && track.volumeLine )
					continue; // the volume line takes the mouse on this track
				ImGui::PushID( k * 100000 + i );
				ImGui::SetCursorScreenPos( va );
				ImGui::InvisibleButton( "##clip", ImVec2( vb.x - va.x, vb.y - va.y ) );
				bool hovered = ImGui::IsItemHovered();
				float mx     = io.MousePos.x;
				bool atIn = mx - a.x < edge && b.x - a.x > edge * 3, atOut = b.x - mx < edge && b.x - a.x > edge * 3;
				if( hovered && ( atIn || atOut ) && state.drag == Drag::None )
					ImGui::SetMouseCursor( ImGuiMouseCursor_ResizeEW );
				// Its keyframes: diamonds along the bottom (a click on one puts the playhead there, a drag moves it,
				// a right-click sets its curve).
				std::vector< double > keyTimes;
				for( double time : tc.KeyTimes() )
					if( time >= -1e-6 && time <= tc.length + 1e-6 )
						keyTimes.push_back( time );
				float keyY  = b.y - Ui( 6 );
				int keyHit  = -1;
				draw->PushClipRect( ImVec2( laneLeft, laneTop ), ImVec2( viewRight, viewBottom ), true );
				for( int n = 0; n < (int)keyTimes.size(); ++n )
				{
					float kx = X( tc.start + keyTimes[ n ] );
					if( kx < laneLeft || kx > viewRight )
						continue;
					bool here = std::fabs( t.position - ( tc.start + keyTimes[ n ] ) ) < 1e-3;
					float r   = Ui( 4 );
					draw->AddQuadFilled( ImVec2( kx, keyY - r ), ImVec2( kx + r, keyY ), ImVec2( kx, keyY + r ), ImVec2( kx - r, keyY ), here ? theme::Accent : IM_COL32( 245, 200, 90, 255 ) );
					draw->AddQuad( ImVec2( kx, keyY - r ), ImVec2( kx + r, keyY ), ImVec2( kx, keyY + r ), ImVec2( kx - r, keyY ), IM_COL32( 0, 0, 0, 200 ) );
					if( std::fabs( io.MousePos.x - kx ) < Ui( 5 ) && std::fabs( io.MousePos.y - keyY ) < Ui( 6 ) )
						keyHit = n;
				}
				draw->PopClipRect();
				if( !tc.clip.effects.empty() && !track.audio )
				{
					// An FX badge at the top right.
					float bx = std::min( b.x, viewRight ) - Ui( 22 );
					if( bx > a.x + Ui( 30 ) )
					{
						draw->AddRectFilled( ImVec2( bx, a.y + Ui( 2 ) ), ImVec2( bx + Ui( 19 ), a.y + Ui( 13 ) ), IM_COL32( 0, 0, 0, 170 ), Ui( 3 ) );
						draw->AddText( Fonts().bold, ImGui::GetFontSize() * 0.7f, ImVec2( bx + Ui( 3 ), a.y + Ui( 2 ) ), theme::Accent, "FX" );
					}
				}
				if( ImGui::IsItemActivated() && keyHit >= 0 )
				{
					SelectClip( k, i );
					state.drag      = Drag::Key;
					state.dragTrack = k;
					state.dragIndex = i;
					state.keyFrom   = keyTimes[ keyHit ];
					state.keyMoved  = false;
				}
				if( keyHit >= 0 && hovered && ImGui::IsMouseClicked( ImGuiMouseButton_Right ) )
				{
					SelectClip( k, i );
					state.keyFrom = keyTimes[ keyHit ];
					ImGui::OpenPopup( "##keyMenu" );
				}
				if( ImGui::BeginPopup( "##keyMenu" ) )
				{
					ImGui::TextDisabled( "Keyframes at %s", t.BarsBeats( tc.start + state.keyFrom ).c_str() );
					ImGui::TextDisabled( "Towards the next one" );
					KeyCurve now = KeyCurve::Linear;
					for( const std::vector< Keyframe >& list : tc.keys )
						if( int at = KeyIndex( list, state.keyFrom ); at >= 0 )
							now = list[ at ].curve;
					for( int c = 0; c < (int)KeyCurve::Count; ++c )
						if( ImGui::MenuItem( KeyCurveName( (KeyCurve)c ), nullptr, now == (KeyCurve)c ) )
							tc.SetCurveAt( state.keyFrom, (KeyCurve)c );
					ImGui::Separator();
					if( ImGui::MenuItem( "Delete" ) )
						tc.RemoveKeysAt( state.keyFrom );
					ImGui::EndPopup();
				}
				else if( ImGui::IsItemActivated() )
				{
					SelectClip( k, i );
					for( int p = 0; p < (int)KeyProperty::Count; ++p )
						state.origKeys[ p ] = tc.keys[ p ];
					state.origParamKeys = tc.paramKeys;
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
				if( ImGui::BeginDragDropTarget() )
				{
					// An effect from the Effects panel: on this clip.
					if( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( kEffectPayload ) )
					{
						app.AddEffect( tc.clip.effects, (const char*)payload->Data );
						SelectClip( k, i );
					}
					ImGui::EndDragDropTarget();
				}
				if( hovered && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) && keyHit < 0 )
					t.Locate( tc.start );
				if( hovered && state.drag == Drag::None && ImGui::IsItemHovered( ImGuiHoveredFlags_DelayNormal ) && !app.learning )
					ImGui::SetTooltip( "%s\n%s - %s (%.2f s)\nDrag: move (Alt: copy, Shift: off the grid); edges: trim\nDouble-click: playhead here; "
					                   "right-click: menu",
					                   tc.clip.name.c_str(), t.BarsBeats( tc.start ).c_str(), t.BarsBeats( tc.End() ).c_str(), tc.length );
				if( ( keyHit < 0 || ImGui::IsPopupOpen( "##clipMenu" ) ) && ImGui::BeginPopupContextItem( "##clipMenu" ) )
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

	// Audio tracks' volume lines: the volume along the timeline, faint when closed, with its points when open.
	// Open, the track is a surface of its own: double-click adds a point, a point drags (Shift: off the grid),
	// right-click sets its curve or deletes it.
	for( int k = 0; k < (int)t.tracks.size(); ++k )
	{
		TimelineTrack& track = t.tracks[ k ];
		if( !track.audio || ( track.volumeKeys.empty() && !track.volumeLine ) )
			continue;
		float top = trackY[ k ] + Ui( 5 ), bottom = trackY[ k ] + trackH[ k ] - Ui( 6 );
		auto Y          = [ & ]( float v ) { return bottom - std::clamp( v, 0.0f, 1.0f ) * ( bottom - top ); };
		auto valueAt    = [ & ]( float my ) { return std::clamp( ( bottom - my ) / std::max( bottom - top, 1.0f ), 0.0f, 1.0f ); };
		bool open       = track.volumeLine;
		ImU32 ink       = open ? IM_COL32( 245, 200, 90, 255 ) : IM_COL32( 245, 200, 90, 110 );
		float visTop    = std::max( trackY[ k ], laneTop ), visBottom = std::min( trackY[ k ] + trackH[ k ], viewBottom );
		if( visBottom - visTop < 2 )
			continue;
		draw->PushClipRect( ImVec2( laneLeft, visTop ), ImVec2( viewRight, visBottom ), true );
		if( open )
			draw->AddRectFilled( ImVec2( laneLeft, visTop ), ImVec2( viewRight, visBottom ), IM_COL32( 0, 0, 0, 70 ) );
		std::vector< ImVec2 > line;
		for( float x = laneLeft; x <= viewRight + Ui( 3 ); x += Ui( 3 ) )
			line.push_back( ImVec2( x, Y( track.VolumeAt( timeAt( x ) ) ) ) );
		draw->AddPolyline( line.data(), (int)line.size(), ink, 0, open ? Ui( 2 ) : Ui( 1.5f ) );
		int hit = -1;
		if( open )
			for( int n = 0; n < (int)track.volumeKeys.size(); ++n )
			{
				const Keyframe& key = track.volumeKeys[ n ];
				ImVec2 p( X( key.time ), Y( key.value ) );
				bool under = std::fabs( io.MousePos.x - p.x ) < Ui( 6 ) && std::fabs( io.MousePos.y - p.y ) < Ui( 6 );
				bool here  = std::fabs( key.time - t.position ) < 1e-3;
				if( under && hit < 0 )
					hit = n;
				draw->AddCircleFilled( p, Ui( 4.5f ), under || ( state.drag == Drag::VolumeKey && state.dragTrack == k && state.dragIndex == n ) ? theme::Text
				                                      : here                                                                          ? theme::Accent
				                                                                                                                      : ink );
				draw->AddCircle( p, Ui( 4.5f ), IM_COL32( 0, 0, 0, 220 ) );
			}
		draw->PopClipRect();
		if( !open )
			continue;

		ImGui::PushID( 900000 + k );
		ImGui::SetCursorScreenPos( ImVec2( laneLeft, visTop ) );
		ImGui::InvisibleButton( "##volumeLine", ImVec2( std::max( viewRight - laneLeft, 1.0f ), visBottom - visTop ),
		                        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight );
		bool hovered = ImGui::IsItemHovered();
		double at    = Snapped( t, timeAt( io.MousePos.x ), freeDrag );
		if( ImGui::IsItemActivated() && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
		{
			if( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) && hit < 0 )
			{
				SetKeyIn( track.volumeKeys, at, valueAt( io.MousePos.y ) );
				hit = KeyIndex( track.volumeKeys, at );
			}
			if( hit >= 0 )
			{
				state.drag      = Drag::VolumeKey;
				state.dragTrack = k;
				state.dragIndex = hit;
				state.keyMoved  = false;
			}
		}
		if( hovered && ImGui::IsMouseClicked( ImGuiMouseButton_Right ) )
		{
			state.dragTrack = k;
			state.dragIndex = hit;
			state.keyFrom   = at;
			state.menuVolume = valueAt( io.MousePos.y ); // where a point added from the menu goes
			ImGui::OpenPopup( "##volumeMenu" );
		}
		if( ImGui::BeginPopup( "##volumeMenu" ) )
		{
			int n = state.dragIndex;
			if( n >= 0 && n < (int)track.volumeKeys.size() )
			{
				Keyframe& key = track.volumeKeys[ n ];
				ImGui::TextDisabled( "Volume %.0f %% at %s", key.value * 100, t.BarsBeats( key.time ).c_str() );
				ImGui::TextDisabled( "Towards the next one" );
				for( int c = 0; c < (int)KeyCurve::Count; ++c )
					if( ImGui::MenuItem( KeyCurveName( (KeyCurve)c ), nullptr, key.curve == (KeyCurve)c ) )
						key.curve = (KeyCurve)c;
				ImGui::Separator();
				if( ImGui::MenuItem( "Delete" ) )
					track.volumeKeys.erase( track.volumeKeys.begin() + n );
			}
			else
			{
				if( ImGui::MenuItem( "Add Point Here" ) )
					SetKeyIn( track.volumeKeys, state.keyFrom, state.menuVolume );
				if( ImGui::MenuItem( "Delete All Points", nullptr, false, !track.volumeKeys.empty() ) )
					track.volumeKeys.clear();
				ImGui::Separator();
				if( ImGui::MenuItem( "Close the Volume Line" ) )
					track.volumeLine = false;
			}
			ImGui::EndPopup();
		}
		if( hovered && state.drag == Drag::None )
			ImGui::SetTooltip( "Volume %.0f %% at %s\nDouble-click: a point; drag a point (Shift: off the grid); right-click: curve, delete",
			                   track.VolumeAt( timeAt( io.MousePos.x ) ) * 100, t.BarsBeats( timeAt( io.MousePos.x ) ).c_str() );
		ImGui::PopID();
	}
	// A volume point being dragged: in time between its neighbours, and up and down.
	if( state.drag == Drag::VolumeKey )
	{
		bool alive = state.dragTrack >= 0 && state.dragTrack < (int)t.tracks.size() && state.dragIndex >= 0
		             && state.dragIndex < (int)t.tracks[ state.dragTrack ].volumeKeys.size();
		if( alive && ImGui::IsMouseDragging( ImGuiMouseButton_Left, Ui( 2 ) ) )
		{
			TimelineTrack& track          = t.tracks[ state.dragTrack ];
			std::vector< Keyframe >& keys = track.volumeKeys;
			int n                         = state.dragIndex;
			float top = trackY[ state.dragTrack ] + Ui( 5 ), bottom = trackY[ state.dragTrack ] + trackH[ state.dragTrack ] - Ui( 6 );
			double lo = n > 0 ? keys[ n - 1 ].time + 0.002 : 0.0, hi = n + 1 < (int)keys.size() ? keys[ n + 1 ].time - 0.002 : 1e12;
			keys[ n ].time  = std::clamp( Snapped( t, timeAt( io.MousePos.x ), freeDrag ), lo, std::max( lo, hi ) );
			keys[ n ].value = std::clamp( ( bottom - io.MousePos.y ) / std::max( bottom - top, 1.0f ), 0.0f, 1.0f );
			state.keyMoved  = true;
			ImGui::SetTooltip( "Volume %.0f %% at %s", keys[ n ].value * 100, t.BarsBeats( keys[ n ].time ).c_str() );
		}
		if( !ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
			state.drag = Drag::None;
	}

	// A keyframe being dragged along its clip (with every keyframe at that moment); a click without moving it puts
	// the playhead there.
	if( state.drag == Drag::Key )
	{
		if( state.dragTrack < (int)t.tracks.size() && state.dragIndex < (int)t.tracks[ state.dragTrack ].clips.size() )
		{
			TimelineClip& tc = t.tracks[ state.dragTrack ].clips[ state.dragIndex ];
			double to        = std::clamp( Snapped( t, timeAt( io.MousePos.x ), freeDrag ) - tc.start, 0.0, tc.length );
			if( ImGui::IsMouseDragging( ImGuiMouseButton_Left, Ui( 3 ) ) && std::fabs( to - state.keyFrom ) > 1e-4 )
			{
				tc.MoveKeysAt( state.keyFrom, to );
				state.keyFrom  = to;
				state.keyMoved = true;
			}
			if( state.keyMoved )
				ImGui::SetTooltip( "Keyframe at %s", t.BarsBeats( tc.start + state.keyFrom ).c_str() );
			if( !ImGui::IsMouseDown( ImGuiMouseButton_Left ) && !state.keyMoved )
				t.Locate( tc.start + state.keyFrom );
		}
		if( !ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
			state.drag = Drag::None;
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
				// The keyframes stay at their moments in time.
				for( int p = 0; p < (int)KeyProperty::Count; ++p )
					tc.keys[ p ] = state.origKeys[ p ];
				tc.paramKeys = state.origParamKeys;
				tc.ShiftKeys( start - state.origStart );
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
					draw->AddRectFilled( a, b, ( theme::Accent & 0x00FFFFFF ) | 0x32000000, Ui( 3 ) );
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
					moved.CopyLookFrom( from[ state.dragIndex ] );
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
	size_t trackCount = t.tracks.size();
	for( int k : order )
		if( t.tracks.size() == trackCount && trackY[ k ] + trackH[ k ] > laneTop && trackY[ k ] < viewBottom )
			TrackHeader( app, k, ImVec2( stickyX, trackY[ k ] ), ImVec2( headerW - Ui( 6 ), trackH[ k ] ) );
	ImGui::PopClipRect();
	// The headers' edge: drag it to make them wider or narrower.
	{
		ImGui::SetCursorScreenPos( ImVec2( stickyX + headerW - Ui( 6 ), laneTop ) );
		ImGui::InvisibleButton( "##headerEdge", ImVec2( Ui( 6 ), std::max( 1.0f, std::min( viewBottom, origin.y + topH + tracksH ) - laneTop ) ) );
		bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
		if( hot )
			ImGui::SetMouseCursor( ImGuiMouseCursor_ResizeEW );
		if( ImGui::IsItemActive() )
			state.headerWidth = std::clamp( state.headerWidth + io.MouseDelta.x * 15.0f / ImGui::GetFontSize(), 160.0f, 520.0f );
		draw->AddLine( ImVec2( stickyX + headerW - Ui( 3 ), laneTop ), ImVec2( stickyX + headerW - Ui( 3 ), std::min( viewBottom, origin.y + topH + tracksH ) ),
		               hot ? theme::Accent : theme::Line, hot ? Ui( 2 ) : Ui( 1 ) );
	}
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

// A keyframe button: a diamond, filled on a keyframe, outlined when the property moves, dim when it does not.
// Returns whether it was clicked.
bool KeyButton( const char* id, bool onKey, bool animated, bool enabled, const char* tip )
{
	float size = ImGui::GetFrameHeight();
	ImVec2 p   = ImGui::GetCursorScreenPos();
	ImGui::BeginDisabled( !enabled );
	bool clicked = ImGui::InvisibleButton( id, ImVec2( size, size ) );
	ImGui::EndDisabled();
	bool hovered = ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled );
	ImDrawList* draw = ImGui::GetWindowDrawList();
	ImVec2 c( p.x + size / 2, p.y + size / 2 );
	float r   = size * 0.28f;
	ImU32 ink = !enabled ? IM_COL32( 255, 255, 255, 40 ) : onKey ? IM_COL32( 245, 200, 90, 255 ) : animated ? theme::Accent : hovered ? theme::Text : theme::TextDim;
	ImVec2 q[ 4 ] = { ImVec2( c.x, c.y - r ), ImVec2( c.x + r, c.y ), ImVec2( c.x, c.y + r ), ImVec2( c.x - r, c.y ) };
	if( onKey )
		draw->AddQuadFilled( q[ 0 ], q[ 1 ], q[ 2 ], q[ 3 ], ink );
	else
		draw->AddQuad( q[ 0 ], q[ 1 ], q[ 2 ], q[ 3 ], ink, Ui( 1.5f ) );
	if( hovered )
		ImGui::SetTooltip( "%s", tip );
	return clicked && enabled;
}

// One property of the selected clip: its keyframe button and its slider. With keyframes, an edit sets the keyframe
// at the playhead (making one if there is none), as in an editing program; without, it sets the value.
void PropertyRow( App& app, TimelineClip& tc, KeyProperty property, const char* label, float min, float max, const char* format, float shown, float reset )
{
	Timeline& t     = app.composition.timeline;
	double local    = t.position - tc.start;
	bool inside     = local >= -1e-6 && local <= tc.length + 1e-6;
	bool animated   = tc.Animated( property );
	bool onKey      = inside && tc.KeyAt( property, local ) >= 0;
	float value     = animated ? tc.Evaluate( property, local ) : tc.Field( property );
	ImGui::PushID( (int)property );
	const char* tip = !inside   ? "Move the playhead over the clip to set keyframes"
	                  : onKey   ? "Remove the keyframe at the playhead"
	                  : animated ? "Add a keyframe at the playhead"
	                             : "Animate: a keyframe at the playhead (then move the playhead and change the value)";
	if( KeyButton( "##key", onKey, animated, inside, tip ) )
	{
		if( onKey )
			tc.RemoveKey( property, local );
		else
			tc.SetKey( property, local, value );
		tc.Apply( t.position );
	}
	ImGui::SameLine( 0, Ui( 2 ) );
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted( label );
	ImGui::SameLine( Ui( 104 ) );
	ImGui::SetNextItemWidth( -FLT_MIN );
	ImGui::BeginDisabled( animated && !inside );
	float edited = value * shown;
	bool changed = FilledSlider( "##value", &edited, min * shown, max * shown, format );
	float plain  = edited / shown;
	if( ImGui::IsItemHovered() && ( ImGui::IsMouseClicked( ImGuiMouseButton_Right ) || ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) ) )
	{
		plain   = reset; // back to its default
		changed = true;
	}
	ImGui::EndDisabled();
	if( changed )
	{
		if( animated )
			tc.SetKey( property, local, plain );
		else
			tc.Field( property ) = plain;
		tc.Apply( t.position );
	}
	ImGui::PopID();
}

void Inspector( App& app )
{
	Timeline& t = app.composition.timeline;
	auto row    = [ & ]( const char* label ) {
		ImGui::AlignTextToFramePadding();
		ImGui::TextDisabled( "%s", label );
		ImGui::SameLine( Ui( 104 ) );
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
		ImGui::PushFont( Fonts().bold );
		ImGui::TextUnformatted( tc->clip.name.c_str() );
		ImGui::PopFont();
		ImGui::TextDisabled( "%s", tc->clip.path.empty() ? tc->clip.generatorId.c_str() : tc->clip.path.c_str() );
		if( !tc->clip.Loaded() && !tc->clip.generator && !tc->clip.audio )
			ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( theme::Error ), "Media not found or not supported" );

		if( ImGui::CollapsingHeader( "Clip", ImGuiTreeNodeFlags_DefaultOpen ) )
		{
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
			ImGui::TextDisabled( "%*s%s   %s", (int)( Ui( 104 ) / ImGui::CalcTextSize( " " ).x ), "", t.BarsBeats( tc->start ).c_str(), t.Timecode( tc->start ).c_str() );
			row( "Length" );
			double length = tc->length;
			if( timeField( "##length", length, 0.05 ) )
			{
				double limit = state.clip + 1 < (int)track.clips.size() ? track.clips[ state.clip + 1 ].start - tc->start : 1e12;
				tc->length   = std::min( length, limit );
			}
			row( "Media in" );
			timeField( "##in", tc->clip.inPoint, 0 );
			row( "Speed" );
			FilledSlider( "##speed", &tc->clip.speed, 0.0f, 4.0f, "%.2fx" );
			ResetValue( &tc->clip.speed, 1.0f );
			row( "Past its end" );
			int mode = tc->clip.playMode == PlayMode::Loop ? 0 : 1;
			if( ImGui::Combo( "##pastEnd", &mode, "Loop the media\0Hold the last frame\0" ) )
				tc->clip.playMode = mode == 0 ? PlayMode::Loop : PlayMode::Once;
		}

		if( !track.audio && ImGui::CollapsingHeader( "Video", ImGuiTreeNodeFlags_DefaultOpen ) )
		{
			row( "Blend" );
			if( ImGui::BeginCombo( "##blend", BlendModeName( tc->blend ), ImGuiComboFlags_HeightLarge ) )
			{
				for( int b = 0; b < (int)BlendMode::Count; ++b )
					if( ImGui::Selectable( BlendModeName( (BlendMode)b ), (int)tc->blend == b ) )
						tc->blend = (BlendMode)b;
				ImGui::EndCombo();
			}
			if( !tc->clip.generator )
			{
				row( "Fit" );
				if( ImGui::BeginCombo( "##fit", FitModeName( tc->clip.fit ) ) )
				{
					for( int f = 0; f < (int)FitMode::Count; ++f )
						if( ImGui::Selectable( FitModeName( (FitMode)f ), (int)tc->clip.fit == f ) )
							tc->clip.fit = (FitMode)f;
					ImGui::EndCombo();
				}
			}
			float W = (float)app.composition.width, H = (float)app.composition.height;
			PropertyRow( app, *tc, KeyProperty::Opacity, "Opacity", 0, 1, "%.0f %%", 100, 1 );
			PropertyRow( app, *tc, KeyProperty::Scale, "Scale", 0, 4, "%.1f %%", 100, 1 );
			PropertyRow( app, *tc, KeyProperty::PositionX, "Position X", -W, W, "%.0f px", 1, 0 );
			PropertyRow( app, *tc, KeyProperty::PositionY, "Position Y", -H, H, "%.0f px", 1, 0 );
			PropertyRow( app, *tc, KeyProperty::Rotation, "Rotation", -360, 360, "%.1f deg", 1, 0 );
		}
		if( tc->clip.audio && ImGui::CollapsingHeader( "Sound", ImGuiTreeNodeFlags_DefaultOpen ) )
			PropertyRow( app, *tc, KeyProperty::Volume, "Volume", 0, 1, "%.0f %%", 100, 1 );

		if( ImGui::CollapsingHeader( "Keyframes", ImGuiTreeNodeFlags_DefaultOpen ) )
		{
			// To the previous and next keyframe of any property, and removing them all.
			std::vector< double > times;
			for( double time : tc->KeyTimes() )
				times.push_back( tc->start + time );
			double before = -1, after = -1;
			for( double time : times )
			{
				if( time < t.position - 1e-3 )
					before = time;
				if( time > t.position + 1e-3 && after < 0 )
					after = time;
			}
			float third = ( ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2 ) / 3;
			ImGui::BeginDisabled( before < 0 );
			if( ImGui::Button( "< Previous", ImVec2( third, 0 ) ) )
				t.Locate( before );
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled( after < 0 );
			if( ImGui::Button( "Next >", ImVec2( third, 0 ) ) )
				t.Locate( after );
			ImGui::EndDisabled();
			ImGui::SameLine();
			ImGui::BeginDisabled( times.empty() );
			if( ImGui::Button( "Clear All", ImVec2( third, 0 ) ) )
			{
				for( std::vector< Keyframe >& list : tc->keys )
					list.clear();
				tc->paramKeys.clear();
			}
			ImGui::EndDisabled();
			// The curve from the keyframes at the playhead to the next ones.
			double local = t.position - tc->start;
			bool atKey   = false;
			KeyCurve now = KeyCurve::Linear;
			for( double time : tc->KeyTimes() )
				atKey |= std::fabs( time - local ) < 1e-3;
			if( atKey )
			{
				for( const std::vector< Keyframe >& list : tc->keys )
					if( int at = KeyIndex( list, local ); at >= 0 )
						now = list[ at ].curve;
				for( const ParamKeys& p : tc->paramKeys )
					if( int at = KeyIndex( p.keys, local ); at >= 0 )
						now = p.keys[ at ].curve;
				row( "Curve" );
				if( ImGui::BeginCombo( "##curve", KeyCurveName( now ) ) )
				{
					for( int c = 0; c < (int)KeyCurve::Count; ++c )
						if( ImGui::Selectable( KeyCurveName( (KeyCurve)c ), now == (KeyCurve)c ) )
							tc->SetCurveAt( local, (KeyCurve)c );
					ImGui::EndCombo();
				}
				if( ImGui::IsItemHovered() )
					ImGui::SetTooltip( "How the values travel from the keyframes at the playhead to the next ones" );
			}
			ImGui::PushTextWrapPos( 0 );
			ImGui::TextDisabled( "Press a diamond to set a keyframe at the playhead; move the playhead and change the value for the next. "
			                     "On the clip: drag a keyframe to move it, right-click it for its curve (Linear, Ease, Hold) or to delete it." );
			ImGui::PopTextWrapPos();
		}

		if( !track.audio && ImGui::CollapsingHeader( "Effects", ImGuiTreeNodeFlags_DefaultOpen ) )
		{
			ImGui::TextDisabled( "Each effect's Mix is how much of it shows; the diamonds keyframe its parameters." );
			// Keyframes for the effects' parameters, as for the clip's own.
			double local = t.position - tc->start;
			bool inside  = local >= -1e-6 && local <= tc->length + 1e-6;
			ParamKeyHooks hooks;
			hooks.button = [ & ]( Effect& effect, int slot, int param ) {
				const std::string& name = effect.Params()[ param ].name;
				ParamKeys* keys         = tc->FindParamKeys( slot, name );
				bool animated           = keys && !keys->keys.empty();
				bool onKey              = inside && keys && KeyIndex( keys->keys, local ) >= 0;
				if( KeyButton( "##paramKey", onKey, animated, inside, !inside ? "Move the playhead over the clip to set keyframes"
				                                                     : onKey  ? "Remove the keyframe at the playhead"
				                                                              : "A keyframe at the playhead" ) )
				{
					keys = tc->FindParamKeys( slot, name, true );
					if( onKey )
						keys->keys.erase( keys->keys.begin() + KeyIndex( keys->keys, local ) );
					else
						SetKeyIn( keys->keys, local, effect.Value( param ) );
				}
			};
			hooks.changed = [ & ]( Effect& effect, int slot, int param, float ) {
				// With keyframes, an edit sets the one at the playhead.
				ParamKeys* keys = tc->FindParamKeys( slot, effect.Params()[ param ].name );
				if( keys && !keys->keys.empty() && inside )
					SetKeyIn( keys->keys, local, effect.Value( param ) );
			};
			hooks.mixButton = [ & ]( float& mix, int slot ) {
				ParamKeys* keys = tc->FindParamKeys( slot, kMixParam );
				bool animated   = keys && !keys->keys.empty();
				bool onKey      = inside && keys && KeyIndex( keys->keys, local ) >= 0;
				if( KeyButton( "##mixKey", onKey, animated, inside, !inside ? "Move the playhead over the clip to set keyframes"
				                                                 : onKey  ? "Remove the keyframe at the playhead"
				                                                          : "A keyframe at the playhead: fade the effect in and out" ) )
				{
					keys = tc->FindParamKeys( slot, kMixParam, true );
					if( onKey )
						keys->keys.erase( keys->keys.begin() + KeyIndex( keys->keys, local ) );
					else
						SetKeyIn( keys->keys, local, mix );
				}
			};
			hooks.mixChanged = [ & ]( float& mix, int slot ) {
				ParamKeys* keys = tc->FindParamKeys( slot, kMixParam );
				if( keys && !keys->keys.empty() && inside )
					SetKeyIn( keys->keys, local, mix );
			};
			// Effects moved or removed in the chain take their keyframes along.
			std::vector< Effect* > before;
			for( EffectSlot& slot : tc->clip.effects )
				before.push_back( slot.effect.get() );
			paramKeyHooks = &hooks;
			DrawEffectChain( app, tc->clip.effects, "timelineFx" );
			paramKeyHooks = nullptr;
			std::vector< ParamKeys > kept;
			for( ParamKeys& p : tc->paramKeys )
			{
				Effect* was = p.slot >= 0 && p.slot < (int)before.size() ? before[ p.slot ] : nullptr;
				for( int s = 0; s < (int)tc->clip.effects.size(); ++s )
					if( was && tc->clip.effects[ s ].effect.get() == was )
					{
						p.slot = s;
						kept.push_back( std::move( p ) );
						break;
					}
			}
			tc->paramKeys = std::move( kept );
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
	ImGui::TextDisabled( "Drag clips from the Clips panel, or files from Files or the desktop, onto a track. Select a clip for its blend, "
	                     "opacity, size, position, rotation and volume, with keyframes, and its effects (drop them from the Effects panel onto "
	                     "the clip). Blocks are the songs: a cue starts each one, and its end action says what follows: Next, Loop, Hold or Black." );
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

	float inspectorW = Ui( 330 );
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
