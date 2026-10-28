#include "Workspaces.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include "Settings.h"

namespace fs = std::filesystem;

namespace stltvj
{
namespace
{
PanelVisibility* visibility = nullptr;

struct Flag
{
	const char* key;
	bool PanelVisibility::*member;
};
const Flag kFlags[] = {
	{ "Clips", &PanelVisibility::clips },
	{ "CompositionMonitor", &PanelVisibility::compositionMonitor },
	{ "PreviewMonitor", &PanelVisibility::previewMonitor },
	{ "Composition", &PanelVisibility::composition },
	{ "Layer", &PanelVisibility::layer },
	{ "Clip", &PanelVisibility::clip },
	{ "Files", &PanelVisibility::files },
	{ "Output", &PanelVisibility::output },
	{ "Performance", &PanelVisibility::performance },
};

// [STLTVJ][Panels] section of the ini: Name=0/1 for each panel.
void* ReadOpen( ImGuiContext*, ImGuiSettingsHandler*, const char* name )
{
	return std::strcmp( name, "Panels" ) == 0 ? (void*)1 : nullptr;
}

void ReadLine( ImGuiContext*, ImGuiSettingsHandler*, void*, const char* line )
{
	if( !visibility )
		return;
	char key[ 64 ] = {};
	int value      = 0;
	if( std::sscanf( line, "%63[^=]=%d", key, &value ) != 2 )
		return;
	for( const Flag& flag : kFlags )
		if( std::strcmp( flag.key, key ) == 0 )
			visibility->*flag.member = value != 0;
}

void WriteAll( ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* out )
{
	if( !visibility )
		return;
	out->appendf( "[%s][Panels]\n", handler->TypeName );
	for( const Flag& flag : kFlags )
		out->appendf( "%s=%d\n", flag.key, visibility->*flag.member ? 1 : 0 );
	out->append( "\n" );
}
}

void Workspaces::Install( PanelVisibility* panels, const std::string& dir )
{
	visibility = panels;
	directory  = dir;
	std::error_code ignored;
	fs::create_directories( fs::u8path( directory ), ignored );

	ImGuiSettingsHandler handler;
	handler.TypeName   = "STLTVJ";
	handler.TypeHash   = ImHashStr( "STLTVJ" );
	handler.ReadOpenFn = ReadOpen;
	handler.ReadLineFn = ReadLine;
	handler.WriteAllFn = WriteAll;
	ImGui::AddSettingsHandler( &handler );
}

std::string Workspaces::PathFor( const std::string& name ) const
{
	return ( fs::u8path( directory ) / fs::u8path( name + ".ini" ) ).u8string();
}

std::vector< std::string > Workspaces::List() const
{
	std::vector< std::string > names;
	std::error_code error;
	for( const fs::directory_entry& entry : fs::directory_iterator( fs::u8path( directory ), error ) )
		if( entry.path().extension() == ".ini" )
			names.push_back( entry.path().stem().u8string() );
	std::sort( names.begin(), names.end() );
	return names;
}

bool Workspaces::Save( const std::string& name )
{
	if( name.empty() || name.find_first_of( "/\\:*?\"<>|" ) != std::string::npos )
		return false;
	size_t size      = 0;
	const char* text = ImGui::SaveIniSettingsToMemory( &size );
	if( !WriteTextFile( PathFor( name ), std::string( text, size ) ) )
		return false;
	current = name;
	return true;
}

bool Workspaces::Remove( const std::string& name )
{
	std::error_code error;
	return fs::remove( fs::u8path( PathFor( name ) ), error );
}

void Workspaces::RequestLoad( const std::string& name )
{
	pending = name;
}

void Workspaces::RequestDefault()
{
	pendingDefault = true;
}

void Workspaces::ApplyPending()
{
	if( pendingDefault )
	{
		pendingDefault = false;
		ImGui::ClearIniSettings();
		*visibility  = PanelVisibility{};
		buildDefault = true;
		current      = "Default";
	}
	if( !pending.empty() )
	{
		std::string text;
		if( ReadTextFile( PathFor( pending ), text ) )
		{
			ImGui::ClearIniSettings();
			ImGui::LoadIniSettingsFromMemory( text.c_str(), text.size() );
			current = pending;
		}
		pending.clear();
	}
}

void Workspaces::BuildDefaultIfNeeded( ImGuiID dockspace, ImVec2 size )
{
	// First run (no saved layout yet) or an explicit reset.
	ImGuiDockNode* node = ImGui::DockBuilderGetNode( dockspace );
	if( !buildDefault && node && ( node->IsSplitNode() || node->Windows.Size > 0 ) )
		return;
	buildDefault = false;

	ImGui::DockBuilderRemoveNode( dockspace );
	ImGui::DockBuilderAddNode( dockspace, ImGuiDockNodeFlags_DockSpace );
	ImGui::DockBuilderSetNodeSize( dockspace, size );

	// Deck on top; monitors, composition/layer, clip and browser side by side below, as in a classic VJ layout.
	ImGuiID top, bottom;
	ImGui::DockBuilderSplitNode( dockspace, ImGuiDir_Up, 0.42f, &top, &bottom );
	ImGuiID monitors, rest;
	ImGui::DockBuilderSplitNode( bottom, ImGuiDir_Left, 0.22f, &monitors, &rest );
	ImGuiID compositionMonitor, previewMonitor;
	ImGui::DockBuilderSplitNode( monitors, ImGuiDir_Up, 0.5f, &compositionMonitor, &previewMonitor );
	ImGuiID params, rest2;
	ImGui::DockBuilderSplitNode( rest, ImGuiDir_Left, 0.34f, &params, &rest2 );
	ImGuiID clipParams, browser;
	ImGui::DockBuilderSplitNode( rest2, ImGuiDir_Left, 0.5f, &clipParams, &browser );

	ImGui::DockBuilderDockWindow( panel::Clips, top );
	ImGui::DockBuilderDockWindow( panel::CompositionMonitor, compositionMonitor );
	ImGui::DockBuilderDockWindow( panel::PreviewMonitor, previewMonitor );
	ImGui::DockBuilderDockWindow( panel::Composition, params );
	ImGui::DockBuilderDockWindow( panel::Layer, params );
	ImGui::DockBuilderDockWindow( panel::Clip, clipParams );
	ImGui::DockBuilderDockWindow( panel::Files, browser );
	ImGui::DockBuilderDockWindow( panel::Output, browser );
	ImGui::DockBuilderDockWindow( panel::Performance, browser );
	ImGui::DockBuilderFinish( dockspace );
}
}
