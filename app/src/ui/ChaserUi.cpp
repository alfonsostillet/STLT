#include "ui/ChaserUi.h"
#include <imgui.h>
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

struct State
{
	int editStep     = 0;
	bool follow      = false; // the edited step follows the one playing
	int painting     = -1;    // a sweep over the slices: -1 none, 0 switching off, 1 on
	std::vector< std::string > swept; // slices already switched by this sweep
	int renaming     = -1;
	std::string renameText;
	FactorySettings factory;
} state;

// Whether `p` is inside the polygon (ray casting; any simple shape).
bool Inside( const std::vector< ImVec2 >& polygon, ImVec2 p )
{
	bool in = false;
	for( size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++ )
	{
		const ImVec2 &a = polygon[ i ], &b = polygon[ j ];
		if( ( a.y > p.y ) != ( b.y > p.y ) && p.x < ( b.x - a.x ) * ( p.y - a.y ) / ( b.y - a.y ) + a.x )
			in = !in;
	}
	return in;
}

void Playback( App& app )
{
	ChaserSetup& c = app.chaser;
	bool changed   = false;
	// On the output: the switch that matters most, in the accent when on.
	if( c.enabled )
	{
		ImGui::PushStyleColor( ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4( theme::Accent ) );
		ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4( theme::Accent ) );
		ImGui::PushStyleColor( ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4( theme::OnAccent ) );
	}
	if( ImGui::Button( c.enabled ? "Chaser ON" : "Chaser OFF", ImVec2( Ui( 100 ), 0 ) ) )
		app.ToggleChaser();
	if( c.enabled )
		ImGui::PopStyleColor( 3 );
	if( ImGui::IsItemHovered() && !app.learning )
		ImGui::SetTooltip( "On: the Advanced Output's slices light step by step (the others dark). Off: every slice shows." );
	MapTarget( app, "/chaser/enabled" );
	ImGui::SameLine();
	if( IconButton( "##chaserPlay", c.playing ? Icon::Pause : Icon::Play, c.playing ) )
	{
		c.playing = !c.playing;
		changed   = true;
	}
	if( ImGui::IsItemHovered() && !app.learning )
		ImGui::SetTooltip( "Play with the beat / hold the step" );
	MapTarget( app, "/chaser/play" );

	ImGui::SameLine( 0, Ui( 14 ) );
	ImGui::SetNextItemWidth( Ui( 90 ) );
	if( ImGui::BeginCombo( "##driver", ChaserDriverName( c.driver ) ) )
	{
		for( int d = 0; d < (int)ChaserDriver::Count; ++d )
			if( ImGui::Selectable( ChaserDriverName( (ChaserDriver)d ), (int)c.driver == d ) )
			{
				c.driver = (ChaserDriver)d;
				changed  = true;
			}
		ImGui::EndCombo();
	}
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "Chaser: the sequence's steps in order. Random: a few slices at random every step." );
	ImGui::SameLine();
	ImGui::SetNextItemWidth( Ui( 110 ) );
	const std::pair< float, const char* > rates[] = { { 0.25f, "1 step a bar" }, { 0.5f, "1 step / 2 beats" }, { 1, "1 step a beat" }, { 2, "2 steps a beat" },
		                                              { 4, "4 steps a beat" }, { 8, "8 steps a beat" } };
	const char* rateName = "custom";
	for( auto [ rate, name ] : rates )
		if( std::fabs( c.stepsPerBeat - rate ) < 1e-4f )
			rateName = name;
	if( ImGui::BeginCombo( "##rate", rateName ) )
	{
		for( auto [ rate, name ] : rates )
			if( ImGui::Selectable( name, std::fabs( c.stepsPerBeat - rate ) < 1e-4f ) )
			{
				c.stepsPerBeat = rate;
				changed        = true;
			}
		ImGui::EndCombo();
	}
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "How fast it steps, locked to the BPM (Resync restarts it with the beat)" );

	auto slider = [ & ]( const char* id, float* value, float min, float max, const char* format, const char* tip ) {
		ImGui::SameLine();
		ImGui::SetNextItemWidth( Ui( 110 ) );
		if( FilledSlider( id, value, min, max, format ) )
			changed = true;
		if( ImGui::IsItemHovered() )
			ImGui::SetTooltip( "%s", tip );
	};
	slider( "##sustain", &c.sustain, 0.05f, 8, "Sustain %.2f", "Steps a slice stays full from its step on" );
	slider( "##release", &c.release, 0, 8, "Release %.2f", "Steps it then takes to fade out" );
	ImGui::SameLine();
	ImGui::SetNextItemWidth( Ui( 90 ) );
	if( ImGui::SliderInt( "##echoes", &c.echoes, 0, 8, "Echoes %d" ) )
		changed = true;
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "The steps before the current one still showing, dimmer and dimmer" );
	if( c.driver == ChaserDriver::Random )
	{
		ImGui::SameLine();
		ImGui::SetNextItemWidth( Ui( 100 ) );
		if( ImGui::SliderInt( "##count", &c.randomCount, 1, 32, "%d slices" ) )
			changed = true;
	}
	float dark = c.dark * 100;
	ImGui::SameLine();
	ImGui::SetNextItemWidth( Ui( 110 ) );
	if( FilledSlider( "##dark", &dark, 0, 100, "Unlit %.0f %%" ) )
	{
		c.dark  = dark / 100;
		changed = true;
	}
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "What the slices not lit show: 0 %% black, 100 %% the full picture" );
	ImGui::SameLine( 0, Ui( 14 ) );
	ImGui::AlignTextToFramePadding();
	int length  = c.Length();
	int playing = (int)( (long long)std::floor( c.position ) % length );
	ImGui::PushFont( Fonts().mono );
	ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( c.enabled ? theme::Accent : theme::TextDim ), "Step %d/%d", playing + 1, length );
	ImGui::PopFont();
	if( changed )
		app.chaserDirty = true;
}

void Sequences( App& app, float height )
{
	ChaserSetup& c = app.chaser;
	ImGui::BeginChild( "##sequences", ImVec2( Ui( 190 ), height ), ImGuiChildFlags_Border );
	ImGui::TextDisabled( "SEQUENCES" );
	int remove = -1, duplicate = -1;
	for( int i = 0; i < (int)c.sequences.size(); ++i )
	{
		ImGui::PushID( i );
		if( state.renaming == i )
		{
			ImGui::SetNextItemWidth( -FLT_MIN );
			if( ImGui::IsWindowAppearing() || !ImGui::IsAnyItemActive() )
				ImGui::SetKeyboardFocusHere();
			if( ImGui::InputText( "##rename", &state.renameText, ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll ) )
			{
				if( !state.renameText.empty() )
					c.sequences[ i ].name = state.renameText;
				state.renaming  = -1;
				app.chaserDirty = true;
			}
			if( ImGui::IsItemDeactivated() )
				state.renaming = -1;
		}
		else
		{
			char label[ 160 ];
			std::snprintf( label, sizeof( label ), "%d  %s", i + 1, c.sequences[ i ].name.c_str() );
			if( ImGui::Selectable( label, c.active == i, ImGuiSelectableFlags_AllowDoubleClick ) )
			{
				c.active        = i;
				app.chaserDirty = true;
				if( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
				{
					state.renaming   = i;
					state.renameText = c.sequences[ i ].name;
				}
			}
			if( ImGui::IsItemHovered() && !app.learning )
				ImGui::SetTooltip( "Click: play it; double-click: rename; right-click: duplicate, delete" );
			if( ImGui::BeginPopupContextItem( "##sequenceMenu" ) )
			{
				if( ImGui::MenuItem( "Rename" ) )
				{
					state.renaming   = i;
					state.renameText = c.sequences[ i ].name;
				}
				if( ImGui::MenuItem( "Duplicate" ) )
					duplicate = i;
				if( ImGui::MenuItem( "Delete", nullptr, false, c.sequences.size() > 1 ) )
					remove = i;
				ImGui::EndPopup();
			}
			MapTarget( app, "/chaser/sequence/" + std::to_string( i + 1 ) );
		}
		ImGui::PopID();
	}
	if( duplicate >= 0 )
	{
		ChaserSequence copy = c.sequences[ duplicate ];
		copy.name += " copy";
		c.sequences.insert( c.sequences.begin() + duplicate + 1, copy );
		c.active        = duplicate + 1;
		app.chaserDirty = true;
	}
	if( remove >= 0 )
	{
		c.sequences.erase( c.sequences.begin() + remove );
		c.active        = std::clamp( c.active, 0, (int)c.sequences.size() - 1 );
		app.chaserDirty = true;
	}
	if( ImGui::Button( "+ Sequence", ImVec2( -FLT_MIN, 0 ) ) )
	{
		ChaserSequence s;
		s.name = "Sequence " + std::to_string( c.sequences.size() + 1 );
		s.Resize( 16 );
		c.sequences.push_back( s );
		c.active        = (int)c.sequences.size() - 1;
		state.editStep  = 0;
		app.chaserDirty = true;
	}
	ImGui::EndChild();
}

// The stage: every slice of the Advanced Output where it takes the composition from, lit in the edited step in
// the accent, and (while the chaser is on) as bright as the output shows it.
void Stage( App& app, ChaserSequence& sequence, ImVec2 size )
{
	ChaserSetup& c = app.chaser;
	ImGui::BeginChild( "##stage", size, ImGuiChildFlags_Border, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
	ImVec2 area = ImGui::GetContentRegionAvail();
	ImVec2 p0   = ImGui::GetCursorScreenPos();
	float cw = (float)std::max( app.composition.width, 1 ), ch = (float)std::max( app.composition.height, 1 );
	float scale = std::min( area.x / cw, area.y / ch ) * 0.96f;
	ImVec2 o( p0.x + ( area.x - cw * scale ) / 2, p0.y + ( area.y - ch * scale ) / 2 );
	auto at          = [ & ]( Vec2 v ) { return ImVec2( o.x + v.x * scale, o.y + v.y * scale ); };
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddRectFilled( o, ImVec2( o.x + cw * scale, o.y + ch * scale ), theme::Well );
	draw->AddRect( o, ImVec2( o.x + cw * scale, o.y + ch * scale ), theme::Line );
	ImGui::InvisibleButton( "##stageArea", area );
	bool hovered = ImGui::IsItemHovered();
	const ImGuiIO& io = ImGui::GetIO();

	int count = 0;
	std::string under;
	for( const Screen& screen : app.advancedOutput.screens )
		for( const Slice& slice : screen.slices )
		{
			if( slice.mask || slice.input.size() < 3 )
				continue;
			++count;
			std::vector< ImVec2 > shape;
			for( Vec2 v : slice.input )
				shape.push_back( at( v ) );
			bool lit   = sequence.Lit( state.editStep, slice.id );
			auto gain  = app.outputs.sliceGains.find( slice.id );
			float live = c.enabled && gain != app.outputs.sliceGains.end() ? gain->second : 0;
			if( lit )
				draw->AddConvexPolyFilled( shape.data(), (int)shape.size(), ( theme::Accent & 0x00FFFFFF ) | 0xB0000000 );
			if( live > 0 )
				draw->AddConvexPolyFilled( shape.data(), (int)shape.size(), IM_COL32( 255, 255, 255, (int)( 90 * live ) ) );
			bool over = hovered && Inside( shape, io.MousePos );
			draw->AddPolyline( shape.data(), (int)shape.size(), over ? theme::Selected : lit ? theme::Accent : IM_COL32( 255, 255, 255, 90 ), ImDrawFlags_Closed,
			                   over ? Ui( 2 ) : Ui( 1 ) );
			// The name, when there is room.
			ImVec2 mn = shape[ 0 ], mx = shape[ 0 ];
			for( ImVec2 v : shape )
			{
				mn = ImVec2( std::min( mn.x, v.x ), std::min( mn.y, v.y ) );
				mx = ImVec2( std::max( mx.x, v.x ), std::max( mx.y, v.y ) );
			}
			ImVec2 t = ImGui::CalcTextSize( slice.name.c_str() );
			if( mx.x - mn.x > t.x + Ui( 6 ) && mx.y - mn.y > t.y + Ui( 4 ) )
				draw->AddText( ImVec2( ( mn.x + mx.x - t.x ) / 2, ( mn.y + mx.y - t.y ) / 2 ), lit ? theme::OnAccent : theme::TextDim, slice.name.c_str() );
			if( over )
				under = slice.id;
		}
	if( count == 0 )
	{
		const char* text = "No slices yet: make them in the Advanced Output (Output > Advanced...).";
		ImVec2 t         = ImGui::CalcTextSize( text );
		draw->AddText( ImVec2( p0.x + ( area.x - t.x ) / 2, p0.y + area.y / 2 ), theme::TextDim, text );
	}
	// A click switches the slice in the edited step; a sweep switches every slice it crosses the same way.
	if( ImGui::IsItemActivated() && !under.empty() && !app.learning )
	{
		state.painting = sequence.Lit( state.editStep, under ) ? 0 : 1;
		state.swept.clear();
	}
	if( state.painting >= 0 )
	{
		if( !under.empty() && std::find( state.swept.begin(), state.swept.end(), under ) == state.swept.end() )
		{
			sequence.Set( state.editStep, under, state.painting == 1 );
			state.swept.push_back( under );
			app.chaserDirty = true;
		}
		if( !ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
			state.painting = -1;
	}
	if( hovered && state.painting < 0 && !app.learning && ImGui::IsItemHovered( ImGuiHoveredFlags_DelayNormal ) )
		ImGui::SetTooltip( "Click a slice to light it in step %d; drag across slices to light (or darken) them all", state.editStep + 1 );
	ImGui::EndChild();
}

void Steps( App& app, ChaserSequence& sequence )
{
	ChaserSetup& c = app.chaser;
	int length     = (int)sequence.steps.size();
	int playing    = (int)( (long long)std::floor( c.position ) % std::max( length, 1 ) );
	if( state.follow && c.playing )
		state.editStep = playing;
	state.editStep = std::clamp( state.editStep, 0, std::max( 0, length - 1 ) );

	if( IconButton( "##prevStep", Icon::Previous ) )
		state.editStep = ( state.editStep - 1 + length ) % length;
	ImGui::SameLine();
	if( IconButton( "##nextStep", Icon::Next ) )
		state.editStep = ( state.editStep + 1 ) % length;
	ImGui::SameLine();
	ImGui::SetNextItemWidth( Ui( 90 ) );
	int steps = length;
	if( ImGui::InputInt( "steps", &steps, 1, 4 ) )
	{
		sequence.Resize( steps );
		app.chaserDirty = true;
	}
	ImGui::SameLine();
	if( ImGui::Button( "->" ) )
	{
		sequence.Repeat( false );
		app.chaserDirty = true;
	}
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "The steps with something lit, repeated over the whole sequence" );
	ImGui::SameLine();
	if( ImGui::Button( "<->" ) )
	{
		sequence.Repeat( true );
		app.chaserDirty = true;
	}
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "The same, back and forth" );
	ImGui::SameLine();
	if( ImGui::Button( "X" ) )
	{
		sequence.Clear();
		app.chaserDirty = true;
	}
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "Empties every step" );
	ImGui::SameLine();
	if( ImGui::Button( "Invert Step" ) )
	{
		std::vector< std::string > lit;
		for( const Screen& screen : app.advancedOutput.screens )
			for( const Slice& slice : screen.slices )
				if( !slice.mask && !sequence.Lit( state.editStep, slice.id ) )
					lit.push_back( slice.id );
		sequence.steps[ state.editStep ] = lit;
		app.chaserDirty                  = true;
	}
	ImGui::SameLine();
	if( ImGui::Button( "Clear Step" ) )
	{
		sequence.steps[ state.editStep ].clear();
		app.chaserDirty = true;
	}
	ImGui::SameLine();
	if( ImGui::Button( "Copy to Next" ) && length > 1 )
	{
		int next              = ( state.editStep + 1 ) % length;
		sequence.steps[ next ] = sequence.steps[ state.editStep ];
		state.editStep         = next;
		app.chaserDirty        = true;
	}
	ImGui::SameLine();
	ImGui::Checkbox( "Follow", &state.follow );
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "The edited step follows the one playing" );

	// The steps: numbers in a row, beats (1, 5, 9...) marked, the one edited framed, the one playing lit.
	float cell = Ui( 30 ), h = Ui( 34 );
	ImGui::BeginChild( "##stepRow", ImVec2( 0, h + Ui( 16 ) ), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar );
	ImDrawList* draw = ImGui::GetWindowDrawList();
	for( int i = 0; i < length; ++i )
	{
		if( i > 0 )
			ImGui::SameLine( 0, Ui( 2 ) );
		ImVec2 p = ImGui::GetCursorScreenPos();
		ImGui::PushID( i );
		if( ImGui::InvisibleButton( "##step", ImVec2( cell, h ) ) )
			state.editStep = i;
		bool hovered = ImGui::IsItemHovered();
		ImGui::PopID();
		bool used = !sequence.steps[ i ].empty();
		ImU32 bg  = i == playing && c.enabled ? theme::Accent : used ? theme::AccentDim : hovered ? theme::RaisedHover : theme::Raised;
		draw->AddRectFilled( p, ImVec2( p.x + cell, p.y + h ), bg, Ui( 3 ) );
		if( i % 4 == 0 )
			draw->AddRectFilled( ImVec2( p.x, p.y + h - Ui( 3 ) ), ImVec2( p.x + cell, p.y + h ), IM_COL32( 255, 255, 255, 60 ), Ui( 3 ) );
		if( i == state.editStep )
			draw->AddRect( p, ImVec2( p.x + cell, p.y + h ), theme::Selected, Ui( 3 ), 0, Ui( 2 ) );
		char number[ 16 ];
		std::snprintf( number, sizeof( number ), "%d", i + 1 );
		ImVec2 t = ImGui::CalcTextSize( number );
		draw->AddText( ImVec2( p.x + ( cell - t.x ) / 2, p.y + ( h - t.y ) / 2 ), i == playing && c.enabled ? theme::OnAccent : theme::Text, number );
		if( hovered && !app.learning )
			ImGui::SetTooltip( "Step %d: %d slice%s", i + 1, (int)sequence.steps[ i ].size(), sequence.steps[ i ].size() == 1 ? "" : "s" );
	}
	ImGui::EndChild();
	// Left and right step through while the panel has the keyboard.
	if( ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) && !ImGui::GetIO().WantTextInput && length > 0 )
	{
		if( ImGui::IsKeyPressed( ImGuiKey_LeftArrow ) )
			state.editStep = ( state.editStep - 1 + length ) % length;
		if( ImGui::IsKeyPressed( ImGuiKey_RightArrow ) )
			state.editStep = ( state.editStep + 1 ) % length;
	}
}

void Factory( App& app, ChaserSequence& sequence )
{
	FactorySettings& f = state.factory;
	ImGui::TextDisabled( "FACTORY" );
	ImGui::SetNextItemWidth( -FLT_MIN );
	if( ImGui::BeginCombo( "##order", FactoryOrderName( f.order ) ) )
	{
		for( int o = 0; o < (int)FactoryOrder::Count; ++o )
			if( ImGui::Selectable( FactoryOrderName( (FactoryOrder)o ), (int)f.order == o ) )
				f.order = (FactoryOrder)o;
		ImGui::EndCombo();
	}
	float tolerance = f.tolerance * 100;
	ImGui::SetNextItemWidth( -FLT_MIN );
	if( FilledSlider( "##tolerance", &tolerance, 0, 20, "Together within %.1f %%" ) )
		f.tolerance = tolerance / 100;
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "Slices this close (share of the composition) light in the same step: a column, a row, a ring" );
	ImGui::SetNextItemWidth( -FLT_MIN );
	ImGui::SliderInt( "##block", &f.block, 1, 8, "%d per step" );
	ImGui::Checkbox( "Mirror", &f.mirror );
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "From both sides at once" );
	ImGui::SameLine();
	ImGui::Checkbox( "Fill", &f.fill );
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "Each step keeps the earlier ones lit" );
	ImGui::Checkbox( "Ping-pong", &f.pingPong );
	if( f.order == FactoryOrder::Random )
	{
		ImGui::SameLine();
		if( ImGui::Button( "New Order" ) )
			f.seed = f.seed * 1664525u + 1013904223u;
	}
	if( ImGui::Button( "Make Steps", ImVec2( -FLT_MIN, 0 ) ) )
	{
		std::vector< std::vector< std::string > > steps = FactorySteps( SlicePlaces( app.advancedOutput ), (float)app.composition.width, (float)app.composition.height, f );
		if( steps.empty() )
			app.SetStatus( "No slices to make steps from: make them in the Advanced Output", true );
		else
		{
			sequence.steps  = steps;
			state.editStep  = 0;
			app.chaserDirty = true;
			app.SetStatus( "Factory: " + std::to_string( steps.size() ) + " steps, " + FactoryOrderName( f.order ) );
		}
	}
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "Replaces this sequence's steps with ones made from where the slices are" );
}
}

void DrawChaserPanel( App& app )
{
	ImGui::SetNextWindowSize( ImVec2( Ui( 1150 ), Ui( 640 ) ), ImGuiCond_FirstUseEver );
	if( !ImGui::Begin( panel::Chaser, &app.panels.chaser ) )
	{
		ImGui::End();
		return;
	}
	ChaserSetup& c = app.chaser;
	if( c.sequences.empty() )
	{
		c.sequences.push_back( {} );
		c.sequences[ 0 ].name = "Sequence 1";
		c.sequences[ 0 ].Resize( 16 );
	}
	ChaserSequence& sequence = *c.Active();
	if( sequence.steps.empty() )
		sequence.Resize( 16 );

	Playback( app );
	ImGui::Separator();
	float bottom = ImGui::GetFrameHeightWithSpacing() * 2 + Ui( 54 );
	float height = std::max( Ui( 120 ), ImGui::GetContentRegionAvail().y - bottom );
	Sequences( app, height );
	ImGui::SameLine();
	float factoryW = Ui( 210 );
	Stage( app, sequence, ImVec2( ImGui::GetContentRegionAvail().x - factoryW - ImGui::GetStyle().ItemSpacing.x, height ) );
	ImGui::SameLine();
	ImGui::BeginChild( "##factory", ImVec2( 0, height ), ImGuiChildFlags_Border );
	Factory( app, sequence );
	ImGui::EndChild();
	Steps( app, sequence );
	ImGui::End();
}
}
