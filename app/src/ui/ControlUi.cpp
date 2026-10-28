#include "ui/ControlUi.h"
#include <imgui.h>
#include <algorithm>
#include "App.h"
#include "ui/Theme.h"
#include "ui/Workspaces.h"

namespace stltvj
{
namespace
{
float Ui( float pixels )
{
	return pixels * ImGui::GetFontSize() / 15.0f;
}
}

void BeginMappable( App& app )
{
	// Disabled items still take the hover, and ImGui's duplicate-ID check then flags the panel holding a hovered
	// control with a red frame: a false alarm while learning, so the check rests meanwhile.
	ImGui::GetIO().ConfigDebugHighlightIdConflicts = !app.learning;
	ImGui::PushStyleVar( ImGuiStyleVar_DisabledAlpha, 1.0f );
	ImGui::BeginDisabled( app.learning );
}

void EndMappable( App& )
{
	ImGui::EndDisabled();
	ImGui::PopStyleVar();
}

void MapTarget( App& app, const std::string& address )
{
	if( !app.learning )
		return;
	ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
	const MidiMapping* mapping = nullptr;
	for( const MidiMapping& m : app.control.midi )
		if( m.address == address )
			mapping = &m;
	bool chosen      = app.learnTarget == address;
	ImDrawList* draw = ImGui::GetWindowDrawList();
	ImU32 fill       = chosen ? IM_COL32( 255, 200, 80, 120 ) : mapping ? IM_COL32( 102, 225, 192, 80 ) : IM_COL32( 80, 140, 255, 70 );
	ImU32 edge       = chosen ? IM_COL32( 255, 200, 80, 255 ) : mapping ? theme::Accent : IM_COL32( 110, 160, 255, 200 );
	draw->AddRectFilled( a, b, fill );
	draw->AddRect( a, b, edge, 0, 0, chosen ? Ui( 2 ) : Ui( 1 ) );
	if( mapping )
	{
		std::string name = MidiKeyName( mapping->key );
		draw->AddText( ImVec2( a.x + Ui( 2 ), a.y ), IM_COL32( 255, 255, 255, 230 ), name.c_str() );
	}
	bool hovered = ImGui::IsMouseHoveringRect( a, b ) && ImGui::IsWindowHovered();
	if( hovered )
		ImGui::SetTooltip( "%s\n%s", address.c_str(), chosen ? "Now move a MIDI control" : "Click, then move a MIDI control to map it" );
	if( hovered && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
		app.learnTarget = address;
	if( hovered && ImGui::IsMouseClicked( ImGuiMouseButton_Right ) && mapping )
	{
		// Right-click: forget the mapping.
		std::string key = MidiKeyName( mapping->key );
		app.control.midi.erase( std::remove_if( app.control.midi.begin(), app.control.midi.end(), [ & ]( const MidiMapping& m ) { return m.address == address; } ),
		                        app.control.midi.end() );
		app.SaveControlSettings();
		app.SetStatus( "Removed the mapping of " + key );
	}
}

void DrawControlPanel( App& app )
{
	if( !ImGui::Begin( panel::Control, &app.panels.control ) )
	{
		ImGui::End();
		return;
	}
	ControlSettings& c = app.control;
	bool changed       = false;

	// Learning.
	if( app.learning )
		ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( 1.0f, 0.62f, 0.2f, 1 ) );
	if( ImGui::Button( app.learning ? "Learning MIDI... (click to stop)" : "Learn MIDI (Ctrl+M)", ImVec2( -FLT_MIN, 0 ) ) )
	{
		app.learning = !app.learning;
		app.learnTarget.clear();
	}
	if( app.learning )
		ImGui::PopStyleColor();
	if( app.learning )
		ImGui::TextWrapped( "%s", app.learnTarget.empty() ? "Click a highlighted control (clips, columns, layer buttons and opacity, BPM, effect parameters...), then "
		                                              "move a knob or press a key. Right-click a mapped control to forget it."
		                                                  : ( "Now move a MIDI control for " + app.learnTarget ).c_str() );

	ImGui::SeparatorText( "MIDI" );
	changed |= ImGui::Checkbox( "MIDI input", &c.midiEnabled );
	std::string midiStatus = app.remote.MidiStatus();
	ImGui::PushTextWrapPos( 0 );
	if( !midiStatus.empty() )
		ImGui::TextColored( ImVec4( 0.95f, 0.6f, 0.4f, 1 ), "%s", midiStatus.c_str() );
	ImGui::PopTextWrapPos();
	std::vector< std::string > devices = app.remote.MidiDevices();
	if( devices.empty() && midiStatus.empty() )
		ImGui::TextDisabled( "No MIDI devices connected" );
	ImGui::BeginDisabled( !c.midiEnabled );
	for( const std::string& name : devices )
	{
		auto off = std::find( c.midiInputsOff.begin(), c.midiInputsOff.end(), name );
		bool on  = off == c.midiInputsOff.end();
		if( ImGui::Checkbox( name.c_str(), &on ) )
		{
			if( on )
				c.midiInputsOff.erase( off );
			else
				c.midiInputsOff.push_back( name );
			changed = true;
		}
	}
	ImGui::EndDisabled();

	if( ImGui::BeginTable( "##mappings", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
	                       ImVec2( 0, std::min( Ui( 220 ), ImGui::GetFrameHeight() * ( c.midi.size() + 1.5f ) ) ) ) )
	{
		ImGui::TableSetupColumn( "MIDI", ImGuiTableColumnFlags_WidthFixed, Ui( 90 ) );
		ImGui::TableSetupColumn( "Control" );
		ImGui::TableSetupColumn( "", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() );
		ImGui::TableHeadersRow();
		int remove = -1;
		for( int i = 0; i < (int)c.midi.size(); ++i )
		{
			ImGui::PushID( i );
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex( 0 );
			ImGui::TextUnformatted( MidiKeyName( c.midi[ i ].key ).c_str() );
			ImGui::TableSetColumnIndex( 1 );
			ImGui::TextUnformatted( c.midi[ i ].address.c_str() );
			ImGui::TableSetColumnIndex( 2 );
			if( ImGui::SmallButton( "x" ) )
				remove = i;
			ImGui::PopID();
		}
		ImGui::EndTable();
		if( remove >= 0 )
		{
			c.midi.erase( c.midi.begin() + remove );
			changed = true;
		}
	}
	if( c.midi.empty() )
		ImGui::TextDisabled( "No mappings: press Learn MIDI." );
	else if( ImGui::SmallButton( "Clear All Mappings" ) )
	{
		c.midi.clear();
		changed = true;
	}

	ImGui::SeparatorText( "OSC" );
	changed |= ImGui::Checkbox( "Receive OSC", &c.oscEnabled );
	ImGui::SameLine();
	ImGui::SetNextItemWidth( Ui( 90 ) );
	int port = c.oscPort;
	if( ImGui::InputInt( "UDP port", &port, 0, 0, ImGuiInputTextFlags_EnterReturnsTrue ) )
	{
		c.oscPort = std::clamp( port, 1, 65535 );
		changed   = true;
	}
	ImGui::PushTextWrapPos( 0 );
	ImGui::TextDisabled( "%s", app.remote.OscStatus().c_str() );
	ImGui::TextDisabled( "Addresses as in Resolume, e.g. /composition/layers/1/clips/2/connect, /composition/layers/1/video/opacity 0.5, "
	                     "/composition/tempocontroller/tempotap. In Learn mode, hovering a control shows its address." );
	ImGui::PopTextWrapPos();

	if( !app.lastControl.empty() )
	{
		ImGui::SeparatorText( "Last received" );
		ImGui::TextWrapped( "%s", app.lastControl.c_str() );
	}

	if( changed )
	{
		app.SaveControlSettings();
		app.remote.Configure( c, true );
	}
	ImGui::End();
}
}
