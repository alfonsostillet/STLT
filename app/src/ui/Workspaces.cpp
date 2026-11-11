#include "Workspaces.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include "Settings.h"
#include "ui/Panels.h"

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
	{ "Layers", &PanelVisibility::layers },
	{ "Timeline", &PanelVisibility::timeline },
	{ "Clips", &PanelVisibility::clips },
	{ "CompositionMonitor", &PanelVisibility::compositionMonitor },
	{ "PreviewMonitor", &PanelVisibility::previewMonitor },
	{ "Composition", &PanelVisibility::composition },
	{ "Layer", &PanelVisibility::layer },
	{ "Clip", &PanelVisibility::clip },
	{ "Files", &PanelVisibility::files },
	{ "Output", &PanelVisibility::output },
	{ "Performance", &PanelVisibility::performance },
	{ "Effects", &PanelVisibility::effects },
	{ "Control", &PanelVisibility::control },
	{ "AdvancedOutput", &PanelVisibility::advancedOutput },
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

void Workspaces::SwitchView( int v )
{
	if( v == view )
		return;
	// The layout in use is kept, on disk too; the other view's comes back.
	size_t size      = 0;
	const char* text = ImGui::SaveIniSettingsToMemory( &size );
	WriteTextFile( ( fs::u8path( directory ) / ( view == 1 ? "arrangement.view" : "session.view" ) ).u8string(), std::string( text, size ) );
	view = v;
	std::string saved;
	if( ReadTextFile( ( fs::u8path( directory ) / ( view == 1 ? "arrangement.view" : "session.view" ) ).u8string(), saved ) && !saved.empty() )
		pendingText = saved;
	else
		pendingDefault = true;
}

void Workspaces::ApplyPending()
{
	if( !pendingText.empty() )
	{
		ImGui::ClearIniSettings();
		ImGui::LoadIniSettingsFromMemory( pendingText.c_str(), pendingText.size() );
		pendingText.clear();
		// In Arrangement the timeline's inspector holds a clip's settings: the deck's Clip and Layer panels rest.
		if( view == 1 )
			visibility->clip = visibility->layer = false;
	}
	if( pendingDefault )
	{
		pendingDefault = false;
		ImGui::ClearIniSettings();
		*visibility = PanelVisibility{};
		if( view == 1 )
		{
			// The Arrangement view: the timeline, and the deck's clips beside to drag onto it.
			visibility->timeline       = true;
			visibility->layers         = false;
			visibility->previewMonitor = false;
			visibility->clip           = false;
			visibility->layer          = false;
		}
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
	{
		// A layout saved before a panel existed (or under its old name): the panel joins the Files panel's tabs
		// instead of floating over everything.
		ImGuiWindowSettings* files = ImGui::FindWindowSettingsByID( ImHashStr( panel::Files ) );
		if( files && files->DockId && !ImGui::FindWindowSettingsByID( ImHashStr( panel::Control ) ) && !ImGui::FindWindowByName( panel::Control ) )
			ImGui::DockBuilderDockWindow( panel::Control, files->DockId );
		// Before the Layers panel the layers' headers were part of Clips: it goes to Clips' left.
		ImGuiWindowSettings* clips = ImGui::FindWindowSettingsByID( ImHashStr( panel::Clips ) );
		ImGuiDockNode* deck        = clips && clips->DockId ? ImGui::DockBuilderGetNode( clips->DockId ) : nullptr;
		if( view == 0 && deck && !ImGui::FindWindowSettingsByID( ImHashStr( panel::Layers ) ) && !ImGui::FindWindowByName( panel::Layers ) )
		{
			ImGuiID left, right;
			float width = std::max( deck->Size.x, 1.0f );
			ImGui::DockBuilderSplitNode( deck->ID, ImGuiDir_Left, std::clamp( LayersPanelWidth() / width, 0.2f, 0.6f ), &left, &right );
			ImGui::DockBuilderDockWindow( panel::Layers, left );
			ImGui::DockBuilderDockWindow( panel::Clips, right );
			ImGui::DockBuilderFinish( dockspace );
		}
		return;
	}
	buildDefault = false;

	ImGui::DockBuilderRemoveNode( dockspace );
	ImGui::DockBuilderAddNode( dockspace, ImGuiDockNodeFlags_DockSpace );
	ImGui::DockBuilderSetNodeSize( dockspace, size );

	if( view == 1 )
	{
		// Arrangement: the timeline across the top; the deck's clips (to drag onto it), the output and the panels
		// below.
		ImGuiID top, bottom, clipsNode, rest, monitor, browser;
		ImGui::DockBuilderSplitNode( dockspace, ImGuiDir_Up, 0.56f, &top, &bottom );
		ImGui::DockBuilderSplitNode( bottom, ImGuiDir_Left, 0.42f, &clipsNode, &rest );
		ImGui::DockBuilderSplitNode( rest, ImGuiDir_Left, 0.42f, &monitor, &browser );
		ImGui::DockBuilderDockWindow( panel::Timeline, top );
		ImGui::DockBuilderDockWindow( panel::Clips, clipsNode );
		ImGui::DockBuilderDockWindow( panel::CompositionMonitor, monitor );
		for( const char* name : { panel::Effects, panel::Files, panel::Output, panel::Control, panel::Composition, panel::Performance } )
			ImGui::DockBuilderDockWindow( name, browser );
		ImGui::DockBuilderFinish( dockspace );
		return;
	}
	// The deck on top, as high as four layers need (no empty space under it); below, side by side: the
	// Composition Monitor and the Preview Monitor, the composition/layer settings, the clip's, and the browser.
	ImGuiID top, bottom;
	float deckRatio = std::clamp( DeckPanelsHeight( std::clamp( deckLayers, 3, 6 ) ) / std::max( size.y, 1.0f ), 0.3f, 0.7f );
	ImGui::DockBuilderSplitNode( dockspace, ImGuiDir_Up, deckRatio, &top, &bottom );
	ImGuiID compositionMonitor, rest;
	ImGui::DockBuilderSplitNode( bottom, ImGuiDir_Left, 0.17f, &compositionMonitor, &rest );
	ImGuiID previewMonitor, rest1;
	ImGui::DockBuilderSplitNode( rest, ImGuiDir_Left, 0.205f, &previewMonitor, &rest1 );
	ImGuiID params, rest2;
	ImGui::DockBuilderSplitNode( rest1, ImGuiDir_Left, 0.34f, &params, &rest2 );
	ImGuiID clipParams, browser;
	ImGui::DockBuilderSplitNode( rest2, ImGuiDir_Left, 0.6f, &clipParams, &browser );

	ImGuiID layers, clips;
	ImGui::DockBuilderSplitNode( top, ImGuiDir_Left, std::clamp( LayersPanelWidth() / std::max( size.x, 1.0f ), 0.2f, 0.6f ), &layers, &clips );
	ImGui::DockBuilderDockWindow( panel::Layers, layers );
	ImGui::DockBuilderDockWindow( panel::Clips, clips );
	ImGui::DockBuilderDockWindow( panel::CompositionMonitor, compositionMonitor );
	ImGui::DockBuilderDockWindow( panel::PreviewMonitor, previewMonitor );
	ImGui::DockBuilderDockWindow( panel::Composition, params );
	ImGui::DockBuilderDockWindow( panel::Layer, params );
	ImGui::DockBuilderDockWindow( panel::Clip, clipParams );
	ImGui::DockBuilderDockWindow( panel::Files, browser );
	ImGui::DockBuilderDockWindow( panel::Effects, browser );
	ImGui::DockBuilderDockWindow( panel::Output, browser );
	ImGui::DockBuilderDockWindow( panel::Performance, browser );
	ImGui::DockBuilderDockWindow( panel::Control, browser );
	ImGui::DockBuilderFinish( dockspace );
}

void Workspaces::Rescale( float ratio )
{
	rescale *= ratio;
}

void Workspaces::ApplyRescale()
{
	if( std::abs( rescale - 1 ) < 1e-4f )
		return;
	float ratio = rescale;
	rescale     = 1;
	ImGuiContext& g = *GImGui;
	// Docked panels keep their share of the window. Floating panels (and those in windows of their own): their size
	// follows.
	for( ImGuiWindow* window : g.Windows )
		if( !window->DockIsActive && !( window->Flags & ( ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_Popup | ImGuiWindowFlags_Tooltip | ImGuiWindowFlags_NoDocking ) )
		    && window->WasActive )
			ImGui::SetWindowSize( window, ImVec2( window->SizeFull.x * ratio, window->SizeFull.y * ratio ) );
	// The deck fits its headers and its layers again.
	FitDeck();
}

void Workspaces::FitDeck()
{
	// The Layers panel, alone in its place at the left of a split (the Clips panel beside it): as wide as the
	// headers. That split, at the top of another (the panels below): as high as the layers.
	ImGuiWindow* layers   = ImGui::FindWindowByName( panel::Layers );
	ImGuiDockNode* node   = layers ? layers->DockNode : nullptr;
	ImGuiDockNode* parent = node ? node->ParentNode : nullptr;
	if( !parent || parent->SplitAxis != ImGuiAxis_X || parent->ChildNodes[ 0 ] != node || node->Windows.Size != 1 || !parent->ChildNodes[ 1 ] )
		return;
	float gutter = ImGui::GetStyle().DockingSeparatorSize;
	float total  = parent->Size.x - gutter;
	if( total > 0 )
	{
		float width                       = std::min( LayersPanelWidth(), total * 0.7f );
		node->SizeRef.x                   = width;
		parent->ChildNodes[ 1 ]->SizeRef.x = std::max( 1.0f, total - width );
	}
	ImGuiDockNode* column = parent->ParentNode;
	if( !column || column->SplitAxis != ImGuiAxis_Y || column->ChildNodes[ 0 ] != parent || !column->ChildNodes[ 1 ] )
		return;
	total = column->Size.y - gutter;
	if( total > 0 )
	{
		float height                       = std::min( DeckPanelsHeight( std::clamp( deckLayers, 1, 8 ) ), total * 0.75f );
		parent->SizeRef.y                  = height;
		column->ChildNodes[ 1 ]->SizeRef.y = std::max( 1.0f, total - height );
	}
}
}
