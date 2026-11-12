#include "ui/ControlUi.h"
#include <imgui.h>
#include <imgui_stdlib.h>
#include <algorithm>
#include "App.h"
#include "Settings.h"
#include "ui/FileDialogs.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"
#include "ui/Workspaces.h"

namespace stltvj
{

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
	// Two separate maps: keys (blue) and MIDI (green). Only the one being mapped shows.
	bool keys = app.learnKeys;
	std::string names;
	if( keys )
	{
		for( const KeyMapping& k : app.control.keys )
			if( k.address == address )
				names += ( names.empty() ? "" : "  " ) + KeyChordName( k.chord );
	}
	else
		for( const MidiMapping& m : app.control.midi )
			if( m.address == address )
				names += ( names.empty() ? "" : "  " ) + MidiKeyName( m.key );
	bool mapped      = !names.empty();
	bool chosen      = app.learnTarget == address;
	ImDrawList* draw = ImGui::GetWindowDrawList();
	ImU32 fill, edge;
	if( keys )
	{
		fill = chosen ? IM_COL32( 90, 150, 255, 170 ) : mapped ? IM_COL32( 70, 120, 255, 120 ) : IM_COL32( 70, 120, 255, 45 );
		edge = chosen ? IM_COL32( 190, 215, 255, 255 ) : IM_COL32( 100, 150, 255, mapped ? 255 : 170 );
	}
	else
	{
		fill = chosen ? IM_COL32( 80, 220, 120, 170 ) : mapped ? IM_COL32( 60, 190, 100, 120 ) : IM_COL32( 60, 190, 100, 45 );
		edge = chosen ? IM_COL32( 190, 255, 205, 255 ) : IM_COL32( 80, 210, 120, mapped ? 255 : 170 );
	}
	draw->AddRectFilled( a, b, fill );
	draw->AddRect( a, b, edge, 0, 0, chosen ? Ui( 2 ) : Ui( 1 ) );
	if( mapped )
	{
		draw->PushClipRect( a, b, true );
		draw->AddText( ImVec2( a.x + Ui( 2 ), a.y ), IM_COL32( 255, 255, 255, 235 ), names.c_str() );
		draw->PopClipRect();
	}
	const char* what = keys ? "press a key" : "move a MIDI control";
	bool hovered     = ImGui::IsMouseHoveringRect( a, b ) && ImGui::IsWindowHovered();
	if( hovered )
		ImGui::SetTooltip( "%s%s%s\n%s%s%s", address.c_str(), mapped ? "\nMapped to " : "", names.c_str(), chosen ? "Now " : "Click, then ", what,
		                   mapped ? "\nRight-click to forget it" : "" );
	if( hovered && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
		app.learnTarget = address;
	if( hovered && ImGui::IsMouseClicked( ImGuiMouseButton_Right ) && mapped )
	{
		// Right-click: forget its keys, or its MIDI, whichever map is open.
		ControlSettings& c = app.control;
		if( keys )
			c.keys.erase( std::remove_if( c.keys.begin(), c.keys.end(), [ & ]( const KeyMapping& k ) { return k.address == address; } ), c.keys.end() );
		else
			c.midi.erase( std::remove_if( c.midi.begin(), c.midi.end(), [ & ]( const MidiMapping& m ) { return m.address == address; } ), c.midi.end() );
		app.SaveControlSettings();
		app.SetStatus( std::string( keys ? "Removed the keys of " : "Removed the MIDI of " ) + address );
	}
}

bool DrawMidiInputs( App& app )
{
	ControlSettings& c = app.control;
	bool changed       = false;
	changed |= ImGui::Checkbox( "MIDI input", &c.midiEnabled );
	ImGui::SameLine();
	changed |= ImGui::Checkbox( "Controller lights", &c.midiLights );
	if( ImGui::IsItemHovered( ImGuiHoveredFlags_DelayNormal ) )
		ImGui::SetTooltip( "Mapped pads and buttons light up on the controller: a clip ready, playing, starting on the beat; a\n"
		                   "switch on. On an APC mini mk2: blue ready, green playing, pulsing green starting." );
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
	return changed;
}

bool DrawOscSettings( App& app )
{
	ControlSettings& c = app.control;
	bool changed       = false;
	changed |= ImGui::Checkbox( "Receive OSC", &c.oscEnabled );
	ImGui::SameLine();
	ImGui::SetNextItemWidth( Ui( 90 ) );
	int port = c.oscPort;
	if( CommittedInputInts( "UDP port", &port, 1 ) )
	{
		c.oscPort = std::clamp( port, 1, 65535 );
		changed   = true;
	}
	ImGui::PushTextWrapPos( 0 );
	ImGui::TextDisabled( "%s", app.remote.OscStatus().c_str() );
	ImGui::TextDisabled( "Addresses as in Resolume, e.g. /composition/layers/1/clips/2/connect, /composition/layers/1/video/opacity 0.5, "
	                     "/composition/tempocontroller/tempotap. While mapping, hovering a control shows its address." );
	ImGui::PopTextWrapPos();
	return changed;
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

	// Learning: one map for the keyboard, one for MIDI.
	auto mapButton = [ & ]( bool keys, const char* label, const char* on, ImVec4 colour ) {
		bool active = app.learning && app.learnKeys == keys;
		if( active )
			ImGui::PushStyleColor( ImGuiCol_Button, colour );
		if( ImGui::Button( active ? on : label, ImVec2( ( ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x ) * ( keys ? 0.5f : 1.0f ), 0 ) ) )
			app.ToggleMapping( keys );
		if( active )
			ImGui::PopStyleColor();
	};
	mapButton( true, "Map Keys (Ctrl+K)", "Mapping keys...", ImVec4( 0.27f, 0.47f, 1.0f, 1 ) );
	ImGui::SameLine();
	mapButton( false, "Map MIDI (Ctrl+M)", "Mapping MIDI...", ImVec4( 0.2f, 0.7f, 0.4f, 1 ) );
	ImGui::PushTextWrapPos( 0 );
	if( app.learning )
	{
		const char* what = app.learnKeys ? "press a key" : "move a MIDI control";
		if( app.learnTarget.empty() )
			ImGui::TextWrapped( "Click a highlighted control (clips, columns, layer buttons, opacity, volume, BPM, effect parameters...), then %s. "
			                    "Right-click a mapped control to forget it. Escape or the same button stops.",
			                    what );
		else
			ImGui::TextWrapped( "Now %s for %s", what, app.learnTarget.c_str() );
	}
	else
		ImGui::TextDisabled( "Keys and MIDI controls launch clips and columns, flip layers and move sliders. Each has its own map: "
		                     "Map Keys shows the keys in blue, Map MIDI shows the MIDI in green." );
	ImGui::PopTextWrapPos();

	// Presets: the maps by name, for other projects, shows and controllers. The maps in use stay loaded whatever
	// project is open.
	ImGui::SeparatorText( "Presets" );
	{
		static std::string presetName, pendingLoad;
		static bool named = false;
		if( !named )
		{
			presetName = app.control.preset;
			named      = true;
		}
		bool changed = app.ShortcutsChanged();
		ImGui::AlignTextToFramePadding();
		if( app.control.preset.empty() )
			ImGui::TextDisabled( "In use: not saved as a preset" );
		else
			ImGui::Text( "In use: %s", app.control.preset.c_str() );
		if( changed )
		{
			ImGui::SameLine();
			ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( theme::Warning ), "(changed, not saved)" );
		}
		std::vector< std::string > presets = app.ShortcutPresets();
		ImGui::SetNextItemWidth( -FLT_MIN );
		if( ImGui::BeginCombo( "##loadShortcuts", "Load preset...", ImGuiComboFlags_HeightLarge ) )
		{
			for( const std::string& name : presets )
				if( ImGui::Selectable( name.c_str(), name == app.control.preset ) )
					pendingLoad = name;
			if( presets.empty() )
				ImGui::TextDisabled( "No presets yet: save one below" );
			ImGui::EndCombo();
		}
		ImGui::SetNextItemWidth( -Ui( 56 ) );
		bool enter = ImGui::InputTextWithHint( "##shortcutPresetName", "preset name", &presetName, ImGuiInputTextFlags_EnterReturnsTrue );
		ImGui::SameLine();
		if( ImGui::Button( "Save", ImVec2( -FLT_MIN, 0 ) ) || enter )
			app.SaveShortcutPreset( presetName );
		if( ImGui::IsItemHovered() )
			ImGui::SetTooltip( "Saves the key and MIDI maps under this name (an existing one is replaced)" );
		float third = ( ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2 ) / 3;
		if( ImGui::Button( "Import...", ImVec2( third, 0 ) ) )
			ShowFileDialog( FileDialog::OpenShortcuts, DocumentsDirectory(), [ &app ]( const std::vector< std::string >& paths ) {
				if( app.ImportShortcuts( paths[ 0 ] ) )
					presetName = app.control.preset;
			} );
		ImGui::SameLine();
		if( ImGui::Button( "Export...", ImVec2( third, 0 ) ) )
			ShowFileDialog( FileDialog::SaveShortcuts, DocumentsDirectory(), [ &app ]( const std::vector< std::string >& paths ) { app.ExportShortcuts( paths[ 0 ] ); },
			                ( app.control.preset.empty() ? std::string( "STLT shortcuts" ) : app.control.preset ) + ".json" );
		ImGui::SameLine();
		ImGui::BeginDisabled( app.control.preset.empty() );
		if( ImGui::Button( "Delete", ImVec2( third, 0 ) ) )
			ImGui::OpenPopup( "##deletePreset" );
		ImGui::EndDisabled();
		if( ImGui::BeginPopup( "##deletePreset" ) )
		{
			ImGui::Text( "Delete the preset \"%s\"? The maps in use stay.", app.control.preset.c_str() );
			if( ImGui::Button( "Delete" ) )
			{
				app.DeleteShortcutPreset( app.control.preset );
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if( ImGui::Button( "Cancel" ) )
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
		// Loading over unsaved changes asks first.
		if( !pendingLoad.empty() )
		{
			if( changed && ( !app.control.keys.empty() || !app.control.midi.empty() ) )
				ImGui::OpenPopup( "##replaceShortcuts" );
			else
			{
				if( app.LoadShortcutPreset( pendingLoad ) )
					presetName = pendingLoad;
				pendingLoad.clear();
			}
		}
		if( ImGui::BeginPopup( "##replaceShortcuts" ) )
		{
			ImGui::Text( "The maps in use have changes that are not saved. Load \"%s\" over them?", pendingLoad.c_str() );
			if( ImGui::Button( "Load" ) )
			{
				if( app.LoadShortcutPreset( pendingLoad ) )
					presetName = pendingLoad;
				pendingLoad.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if( ImGui::Button( "Cancel" ) )
			{
				pendingLoad.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
		else if( !pendingLoad.empty() && !ImGui::IsPopupOpen( "##replaceShortcuts" ) )
			pendingLoad.clear();
	}

	ImGui::SeparatorText( "Keyboard" );
	if( !c.keys.empty()
	    && ImGui::BeginTable( "##keys", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY,
	                          ImVec2( 0, std::min( Ui( 220 ), ImGui::GetFrameHeight() * ( c.keys.size() + 1.5f ) ) ) ) )
	{
		ImGui::TableSetupColumn( "Key", ImGuiTableColumnFlags_WidthFixed, Ui( 90 ) );
		ImGui::TableSetupColumn( "Control" );
		ImGui::TableSetupColumn( "", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight() );
		ImGui::TableHeadersRow();
		int remove = -1;
		for( int i = 0; i < (int)c.keys.size(); ++i )
		{
			ImGui::PushID( i );
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex( 0 );
			ImGui::TextUnformatted( KeyChordName( c.keys[ i ].chord ).c_str() );
			ImGui::TableSetColumnIndex( 1 );
			ImGui::TextUnformatted( c.keys[ i ].address.c_str() );
			ImGui::TableSetColumnIndex( 2 );
			if( ImGui::SmallButton( "x" ) )
				remove = i;
			ImGui::PopID();
		}
		ImGui::EndTable();
		if( remove >= 0 )
		{
			c.keys.erase( c.keys.begin() + remove );
			changed = true;
		}
	}
	if( c.keys.empty() )
		ImGui::TextDisabled( "No keys mapped." );
	else if( ImGui::SmallButton( "Clear All Keys" ) )
	{
		c.keys.clear();
		changed = true;
	}

	ImGui::SeparatorText( "MIDI" );
	changed |= DrawMidiInputs( app );

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
		ImGui::TextDisabled( "No MIDI mapped." );
	else if( ImGui::SmallButton( "Clear All MIDI" ) )
	{
		c.midi.clear();
		changed = true;
	}

	ImGui::SeparatorText( "OSC" );
	changed |= DrawOscSettings( app );

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
