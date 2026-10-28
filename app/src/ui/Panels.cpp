#include "ui/Panels.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include "App.h"
#include "Settings.h"
#include "media/ThreadPool.h"
#include "media/FrameSource.h"
#include "ui/ControlUi.h"
#include "ui/EffectsUi.h"
#include "ui/FileDialogs.h"
#include "ui/Theme.h"

namespace fs = std::filesystem;

namespace stltvj
{
namespace
{
constexpr const char* kCellPayload = "STLT_CELL";
constexpr const char* kFilePayload = "STLT_FILE";

struct CellRef
{
	int layer, column;
};

float Ui( float pixels )
{
	return pixels * ImGui::GetFontSize() / 15.0f;
}

ImTextureID TextureId( GLuint texture )
{
	return (ImTextureID)(intptr_t)texture;
}

// FBO textures have their first row at the bottom.
void FboImage( GLuint texture, ImVec2 size )
{
	ImGui::Image( TextureId( texture ), size, ImVec2( 0, 1 ), ImVec2( 1, 0 ) );
}

// Largest size with the given aspect that fits `area`, and the offset that centres it.
ImVec2 FitSize( ImVec2 area, float aspect, ImVec2* offset )
{
	ImVec2 size( area.x, area.x / aspect );
	if( size.y > area.y )
		size = ImVec2( area.y * aspect, area.y );
	size.x  = std::max( size.x, 1.0f );
	size.y  = std::max( size.y, 1.0f );
	*offset = ImVec2( ( area.x - size.x ) / 2, ( area.y - size.y ) / 2 );
	return size;
}

bool IsMediaFile( const fs::path& path )
{
	return IsMediaExtension( path.extension().u8string() );
}

bool IsComposition( const fs::path& path )
{
	return path.extension() == ".stltvj";
}

std::string FormatTime( double seconds )
{
	if( seconds < 0 )
		seconds = 0;
	int minutes = (int)( seconds / 60 );
	double rest = seconds - minutes * 60;
	char text[ 32 ];
	std::snprintf( text, sizeof( text ), "%02d:%05.2f", minutes, rest );
	return text;
}

// Label in the left column, control filling the right: the look of a parameter list.
bool BeginParams( const char* id )
{
	if( !ImGui::BeginTable( id, 2, ImGuiTableFlags_SizingStretchProp ) )
		return false;
	ImGui::TableSetupColumn( "label", ImGuiTableColumnFlags_WidthFixed, Ui( 86 ) );
	ImGui::TableSetupColumn( "value", ImGuiTableColumnFlags_WidthStretch );
	return true;
}

void ParamLabel( const char* label )
{
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex( 0 );
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted( label );
	ImGui::TableSetColumnIndex( 1 );
	ImGui::SetNextItemWidth( -FLT_MIN );
}

bool PercentSlider( const char* id, float* value, float max = 1.0f )
{
	float percent = *value * 100.0f;
	bool changed  = ImGui::SliderFloat( id, &percent, 0.0f, max * 100.0f, "%.0f %%" );
	if( changed )
		*value = percent / 100.0f;
	if( ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
	{
		*value  = 1.0f;
		changed = true;
	}
	return changed;
}

template< typename Enum >
bool EnumCombo( const char* id, Enum* value, const char* ( *name )( Enum ) )
{
	bool changed = false;
	if( ImGui::BeginCombo( id, name( *value ) ) )
	{
		for( int i = 0; i < (int)Enum::Count; ++i )
		{
			bool selected = (int)*value == i;
			if( ImGui::Selectable( name( (Enum)i ), selected ) )
			{
				*value  = (Enum)i;
				changed = true;
			}
		}
		ImGui::EndCombo();
	}
	return changed;
}

void DrawIcon( ImDrawList* draw, Icon icon, ImVec2 min, ImVec2 max, ImU32 colour )
{
	ImVec2 c( ( min.x + max.x ) / 2, ( min.y + max.y ) / 2 );
	float r = std::min( max.x - min.x, max.y - min.y ) * 0.28f;
	switch( icon )
	{
	case Icon::Play: draw->AddTriangleFilled( ImVec2( c.x - r * 0.8f, c.y - r ), ImVec2( c.x - r * 0.8f, c.y + r ), ImVec2( c.x + r, c.y ), colour ); break;
	case Icon::Reverse:
		draw->AddTriangleFilled( ImVec2( c.x + r * 0.8f, c.y - r ), ImVec2( c.x + r * 0.8f, c.y + r ), ImVec2( c.x - r, c.y ), colour );
		break;
	case Icon::Pause:
		draw->AddRectFilled( ImVec2( c.x - r * 0.8f, c.y - r ), ImVec2( c.x - r * 0.25f, c.y + r ), colour );
		draw->AddRectFilled( ImVec2( c.x + r * 0.25f, c.y - r ), ImVec2( c.x + r * 0.8f, c.y + r ), colour );
		break;
	case Icon::Stop: draw->AddRectFilled( ImVec2( c.x - r * 0.8f, c.y - r * 0.8f ), ImVec2( c.x + r * 0.8f, c.y + r * 0.8f ), colour ); break;
	case Icon::Close:
		draw->AddLine( ImVec2( c.x - r * 0.7f, c.y - r * 0.7f ), ImVec2( c.x + r * 0.7f, c.y + r * 0.7f ), colour, Ui( 1.6f ) );
		draw->AddLine( ImVec2( c.x - r * 0.7f, c.y + r * 0.7f ), ImVec2( c.x + r * 0.7f, c.y - r * 0.7f ), colour, Ui( 1.6f ) );
		break;
	}
}

// A small square toggle button with a letter on it (layer B / S switches).
bool LetterButton( const char* id, const char* letter, bool on, ImU32 onColour )
{
	float size = ImGui::GetFrameHeight();
	ImGui::PushID( id );
	ImVec2 pos   = ImGui::GetCursorScreenPos();
	bool clicked = ImGui::InvisibleButton( "##b", ImVec2( size, size ) );
	ImDrawList* draw = ImGui::GetWindowDrawList();
	ImU32 bg = on ? onColour : ImGui::GetColorU32( ImGui::IsItemHovered() ? ImGuiCol_ButtonHovered : ImGuiCol_Button );
	draw->AddRectFilled( pos, ImVec2( pos.x + size, pos.y + size ), bg, Ui( 2 ) );
	ImVec2 textSize = ImGui::CalcTextSize( letter );
	draw->AddText( ImVec2( pos.x + ( size - textSize.x ) / 2, pos.y + ( size - textSize.y ) / 2 ), on ? IM_COL32( 20, 20, 20, 255 ) : IM_COL32( 220, 220, 220, 255 ),
	               letter );
	ImGui::PopID();
	return clicked;
}

// Directory listings are cached and refreshed every couple of seconds.
struct Listing
{
	std::string directory;
	double readAt = -1;
	std::vector< fs::path > folders;
	std::vector< fs::path > files;
};

const Listing& ListDirectory( const std::string& directory )
{
	static Listing listing;
	double now = glfwGetTime();
	if( listing.directory == directory && now - listing.readAt < 2.0 )
		return listing;
	listing           = Listing{};
	listing.directory = directory;
	listing.readAt    = now;
	std::error_code error;
	for( const fs::directory_entry& entry : fs::directory_iterator( fs::u8path( directory ), fs::directory_options::skip_permission_denied, error ) )
	{
		std::string name = entry.path().filename().u8string();
		if( name.empty() || name[ 0 ] == '.' )
			continue;
		std::error_code ignored;
		if( entry.is_directory( ignored ) )
			listing.folders.push_back( entry.path() );
		else if( IsMediaFile( entry.path() ) || IsComposition( entry.path() ) )
			listing.files.push_back( entry.path() );
	}
	auto byName = []( const fs::path& a, const fs::path& b ) { return a.filename().u8string() < b.filename().u8string(); };
	std::sort( listing.folders.begin(), listing.folders.end(), byName );
	std::sort( listing.files.begin(), listing.files.end(), byName );
	return listing;
}

// Loads `paths` into consecutive cells of a layer, starting at `column`.
void LoadInto( App& app, int layer, int column, const std::vector< std::string >& paths )
{
	for( const std::string& path : paths )
	{
		if( IsComposition( fs::u8path( path ) ) )
		{
			app.OpenComposition( path );
			return;
		}
		if( !IsMediaFile( fs::u8path( path ) ) )
			continue;
		app.LoadClip( layer, column++, path );
	}
}

int FirstEmptyColumn( const Layer& layer )
{
	for( int c = 0; c < (int)layer.clips.size(); ++c )
		if( layer.clips[ c ].Empty() )
			return c;
	return (int)layer.clips.size();
}

void LoadIntoSelected( App& app, const std::vector< std::string >& paths )
{
	Composition& comp = app.composition;
	if( comp.layers.empty() )
		comp.AddLayer();
	int layer  = std::clamp( comp.selectedLayer, 0, (int)comp.layers.size() - 1 );
	int column = comp.selectedColumn >= 0 && comp.layers[ layer ].clips[ comp.selectedColumn ].Empty() ? comp.selectedColumn
	                                                                                                      : FirstEmptyColumn( comp.layers[ layer ] );
	LoadInto( app, layer, column, paths );
}

std::string DefaultCompositionPath( App& app )
{
	if( !app.compositionPath.empty() )
		return app.compositionPath;
	return ( fs::u8path( HomeDirectory() ) / "STLT-VJ" / fs::u8path( app.composition.name + ".stltvj" ) ).u8string();
}
}

bool IconButton( const char* id, Icon icon, bool active, float size )
{
	if( size <= 0 )
		size = ImGui::GetFrameHeight();
	ImVec2 pos       = ImGui::GetCursorScreenPos();
	bool clicked     = ImGui::InvisibleButton( id, ImVec2( size * 1.3f, size ) );
	bool hovered     = ImGui::IsItemHovered();
	ImDrawList* draw = ImGui::GetWindowDrawList();
	ImU32 bg         = active ? theme::Accent : ImGui::GetColorU32( hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button );
	ImVec2 max( pos.x + size * 1.3f, pos.y + size );
	draw->AddRectFilled( pos, max, bg, Ui( 2 ) );
	DrawIcon( draw, icon, pos, max, active ? IM_COL32( 20, 20, 20, 255 ) : IM_COL32( 225, 225, 225, 255 ) );
	return clicked;
}

void DrawMainMenu( App& app )
{
	bool openComposition = false, saveCompositionAs = false, saveWorkspace = false, openPath = false, savePath = false;
	if( ImGui::BeginMenuBar() )
	{
		if( ImGui::BeginMenu( "Composition" ) )
		{
			if( ImGui::MenuItem( "New" ) )
				app.NewComposition();
			if( ImGui::MenuItem( "Open...", "Ctrl+O" ) )
				openComposition = true;
			if( ImGui::MenuItem( "Save", "Ctrl+S" ) )
			{
				if( app.compositionPath.empty() )
					saveCompositionAs = true;
				else
					app.SaveCompositionTo( app.compositionPath );
			}
			if( ImGui::MenuItem( "Save As..." ) )
				saveCompositionAs = true;
			ImGui::Separator();
			if( ImGui::MenuItem( "Import Media...", "Ctrl+I" ) )
				ShowFileDialog( FileDialog::OpenMedia, app.browserDirectory, [ &app ]( const std::vector< std::string >& paths ) { LoadIntoSelected( app, paths ); } );
			if( ImGui::BeginMenu( "Type a Path" ) )
			{
				if( ImGui::MenuItem( "Open Path..." ) )
					openPath = true;
				if( ImGui::MenuItem( "Save to Path..." ) )
					savePath = true;
				ImGui::EndMenu();
			}
			ImGui::Separator();
			if( ImGui::MenuItem( "Quit" ) )
				app.quitRequested = true;
			ImGui::EndMenu();
		}
		if( ImGui::BeginMenu( "Edit" ) )
		{
			std::string undo = app.history.CanUndo() ? "Undo " + app.history.UndoLabel() : "Undo";
			std::string redo = app.history.CanRedo() ? "Redo " + app.history.RedoLabel() : "Redo";
			if( ImGui::MenuItem( undo.c_str(), "Ctrl+Z", false, app.history.CanUndo() ) )
				app.Undo();
			if( ImGui::MenuItem( redo.c_str(), "Ctrl+Shift+Z", false, app.history.CanRedo() ) )
				app.Redo();
			ImGui::EndMenu();
		}
		if( ImGui::BeginMenu( "Layer" ) )
		{
			if( ImGui::MenuItem( "New Layer" ) )
				app.composition.AddLayer();
			if( ImGui::MenuItem( "Clear All Layers" ) )
				for( int l = 0; l < (int)app.composition.layers.size(); ++l )
					app.ClearLayer( l );
			ImGui::EndMenu();
		}
		if( ImGui::BeginMenu( "Column" ) )
		{
			if( ImGui::MenuItem( "New Column" ) )
				app.composition.AddColumn();
			ImGui::EndMenu();
		}
		if( ImGui::BeginMenu( "Output" ) )
		{
			if( ImGui::MenuItem( "Advanced Output...", "Ctrl+Shift+A" ) )
				app.panels.advancedOutput = true;
			ImGui::Separator();
			ImGui::TextDisabled( "Quick: first screen" );
			for( const OutputWindow::Display& d : OutputWindow::Displays() )
			{
				std::string label = "Fullscreen on " + d.name + "  (" + std::to_string( d.width ) + "x" + std::to_string( d.height ) + ")";
				if( ImGui::MenuItem( label.c_str() ) )
					app.ShowScreenOn( 0, OutputDevice::Display, d.monitorName );
			}
			if( ImGui::MenuItem( "In a Window" ) )
				app.ShowScreenOn( 0, OutputDevice::Window );
			ImGui::Separator();
			if( ImGui::MenuItem( "Stop All Outputs", nullptr, false, app.outputs.AnyWindowOpen() ) )
				app.StopOutputs();
			ImGui::EndMenu();
		}
		if( ImGui::BeginMenu( "View" ) )
		{
			PanelVisibility& p = app.panels;
			ImGui::MenuItem( panel::Clips, nullptr, &p.clips );
			ImGui::MenuItem( panel::CompositionMonitor, nullptr, &p.compositionMonitor );
			ImGui::MenuItem( panel::PreviewMonitor, nullptr, &p.previewMonitor );
			ImGui::MenuItem( panel::Composition, nullptr, &p.composition );
			ImGui::MenuItem( panel::Layer, nullptr, &p.layer );
			ImGui::MenuItem( panel::Clip, nullptr, &p.clip );
			ImGui::MenuItem( panel::Files, nullptr, &p.files );
			ImGui::MenuItem( panel::Output, nullptr, &p.output );
			ImGui::MenuItem( panel::Performance, nullptr, &p.performance );
			ImGui::MenuItem( panel::Effects, nullptr, &p.effects );
			ImGui::MenuItem( panel::Control, nullptr, &p.control );
			ImGui::MenuItem( panel::AdvancedOutput, nullptr, &p.advancedOutput );
			ImGui::EndMenu();
		}
		if( ImGui::BeginMenu( "Workspace" ) )
		{
			for( const std::string& name : app.workspaces.List() )
				if( ImGui::MenuItem( name.c_str(), nullptr, name == app.workspaces.Current() ) )
					app.workspaces.RequestLoad( name );
			ImGui::Separator();
			if( ImGui::MenuItem( "Save Workspace As..." ) )
				saveWorkspace = true;
			if( ImGui::MenuItem( "Reset to Default Layout" ) )
				app.workspaces.RequestDefault();
			if( ImGui::BeginMenu( "Delete" ) )
			{
				for( const std::string& name : app.workspaces.List() )
					if( ImGui::MenuItem( name.c_str() ) )
						app.workspaces.Remove( name );
				ImGui::EndMenu();
			}
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}

	ImGuiIO& io = ImGui::GetIO();
	if( io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_O, false ) )
		openComposition = true;
	// Text fields keep their own undo.
	if( io.KeyCtrl && !io.WantTextInput && ImGui::IsKeyPressed( ImGuiKey_Z, false ) )
	{
		if( io.KeyShift )
			app.Redo();
		else
			app.Undo();
	}
	if( io.KeyCtrl && !io.WantTextInput && ImGui::IsKeyPressed( ImGuiKey_Y, false ) )
		app.Redo();
	if( io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed( ImGuiKey_A, false ) )
		app.panels.advancedOutput = !app.panels.advancedOutput;
	if( io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_I, false ) )
		ShowFileDialog( FileDialog::OpenMedia, app.browserDirectory, [ &app ]( const std::vector< std::string >& paths ) { LoadIntoSelected( app, paths ); } );
	if( io.KeyCtrl && ImGui::IsKeyPressed( ImGuiKey_S, false ) )
	{
		if( app.compositionPath.empty() )
			saveCompositionAs = true;
		else
			app.SaveCompositionTo( app.compositionPath );
	}

	static std::string pathInput, workspaceName;
	if( openComposition )
		ShowFileDialog( FileDialog::OpenComposition, app.compositionPath.empty() ? app.browserDirectory : app.compositionPath,
		                [ &app ]( const std::vector< std::string >& paths ) { app.OpenComposition( paths[ 0 ] ); } );
	if( saveCompositionAs )
	{
		std::string suggested = DefaultCompositionPath( app );
		ShowFileDialog(
			FileDialog::SaveComposition, suggested, [ &app ]( const std::vector< std::string >& paths ) { app.SaveCompositionTo( paths[ 0 ] ); },
			fs::u8path( suggested ).filename().u8string() );
	}
	if( openPath )
	{
		pathInput = app.compositionPath.empty() ? app.browserDirectory : app.compositionPath;
		ImGui::OpenPopup( "Open Composition" );
	}
	if( savePath )
	{
		pathInput = DefaultCompositionPath( app );
		ImGui::OpenPopup( "Save Composition" );
	}
	if( saveWorkspace )
	{
		workspaceName = app.workspaces.Current();
		ImGui::OpenPopup( "Save Workspace" );
	}

	ImGui::SetNextWindowSize( ImVec2( Ui( 560 ), 0 ) );
	if( ImGui::BeginPopupModal( "Open Composition", nullptr, ImGuiWindowFlags_NoResize ) )
	{
		ImGui::TextUnformatted( "Composition file (.stltvj). You can also double-click one in the Files panel." );
		ImGui::SetNextItemWidth( -FLT_MIN );
		bool enter = ImGui::InputText( "##path", &pathInput, ImGuiInputTextFlags_EnterReturnsTrue );
		if( enter || ImGui::Button( "Open" ) )
		{
			if( app.OpenComposition( pathInput ) )
				ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if( ImGui::Button( "Cancel" ) )
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
	ImGui::SetNextWindowSize( ImVec2( Ui( 560 ), 0 ) );
	if( ImGui::BeginPopupModal( "Save Composition", nullptr, ImGuiWindowFlags_NoResize ) )
	{
		ImGui::SetNextItemWidth( -FLT_MIN );
		bool enter = ImGui::InputText( "##path", &pathInput, ImGuiInputTextFlags_EnterReturnsTrue );
		if( enter || ImGui::Button( "Save" ) )
		{
			std::error_code ignored;
			fs::create_directories( fs::u8path( pathInput ).parent_path(), ignored );
			if( app.SaveCompositionTo( pathInput ) )
				ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if( ImGui::Button( "Cancel" ) )
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
	if( ImGui::BeginPopupModal( "Save Workspace", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) )
	{
		ImGui::TextUnformatted( "Saves where every panel is, docked or floating." );
		ImGui::SetNextItemWidth( Ui( 300 ) );
		bool enter = ImGui::InputText( "Name", &workspaceName, ImGuiInputTextFlags_EnterReturnsTrue );
		if( enter || ImGui::Button( "Save" ) )
		{
			if( app.workspaces.Save( workspaceName ) )
			{
				app.SetStatus( "Workspace \"" + workspaceName + "\" saved" );
				ImGui::CloseCurrentPopup();
			}
			else
			{
				app.SetStatus( "Could not save the workspace (use a name without / \\ : * ? \" < > |)", true );
			}
		}
		ImGui::SameLine();
		if( ImGui::Button( "Cancel" ) )
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
}

void DrawToolbar( App& app )
{
	Composition& comp = app.composition;
	ImGuiStyle& style = ImGui::GetStyle();
	ImGui::SetCursorPos( ImVec2( style.WindowPadding.x + Ui( 6 ), ImGui::GetCursorPosY() + Ui( 4 ) ) );
	ImGui::BeginGroup();
	if( IconButton( "##play", comp.paused ? Icon::Play : Icon::Pause, false ) )
		comp.paused = !comp.paused;
	MapTarget( app, "/composition/pause" );
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( comp.paused ? "Resume (Space)" : "Pause everything (Space)" );
	ImGui::SameLine();
	if( IconButton( "##stop", Icon::Stop ) )
	{
		for( int l = 0; l < (int)comp.layers.size(); ++l )
			app.ClearLayer( l );
		comp.activeColumn = -1;
	}
	MapTarget( app, "/composition/stop" );
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "Clear every layer" );
	if( !ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed( ImGuiKey_Space, false ) )
		comp.paused = !comp.paused;

	ImGui::SameLine( 0, Ui( 18 ) );
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted( "BPM" );
	ImGui::SameLine();
	float bpm = (float)comp.bpm;
	ImGui::SetNextItemWidth( Ui( 64 ) );
	if( ImGui::DragFloat( "##bpm", &bpm, 0.1f, 20.0f, 500.0f, "%.2f" ) )
		comp.bpm = bpm;
	MapTarget( app, "/composition/tempocontroller/tempo" );
	ImGui::SameLine();
	if( ImGui::Button( "/2" ) )
		comp.bpm = std::max( 20.0, comp.bpm / 2 );
	ImGui::SameLine();
	if( ImGui::Button( "x2" ) )
		comp.bpm = std::min( 500.0, comp.bpm * 2 );
	ImGui::SameLine();
	if( ImGui::Button( "TAP" ) )
		app.TapTempo();
	MapTarget( app, "/composition/tempocontroller/tempotap" );
	ImGui::SameLine();
	if( ImGui::Button( "RESYNC" ) )
		app.ResyncBeat();
	MapTarget( app, "/composition/tempocontroller/resync" );
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "Start the beat over: the next beat begins now" );
	// Beat light.
	ImGui::SameLine( 0, Ui( 10 ) );
	double beat      = comp.beat;
	float flash      = (float)( 1.0 - ( beat - std::floor( beat ) ) );
	ImVec2 p         = ImGui::GetCursorScreenPos();
	float h          = ImGui::GetFrameHeight();
	float glow       = flash * flash;
	ImU32 beatColour = ImGui::GetColorU32( ImVec4( 0.2f + 0.2f * glow, 0.2f + 0.68f * glow, 0.2f + 0.55f * glow, 1 ) );
	ImGui::GetWindowDrawList()->AddCircleFilled( ImVec2( p.x + h / 2, p.y + h / 2 ), h * 0.22f, beatColour );
	ImGui::Dummy( ImVec2( h, h ) );

	ImGui::SameLine( 0, Ui( 18 ) );
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted( "Speed" );
	ImGui::SameLine();
	ImGui::SetNextItemWidth( Ui( 90 ) );
	ImGui::SliderFloat( "##speed", &comp.speed, 0.0f, 4.0f, "%.2f" );
	if( ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
		comp.speed = 1.0f;
	MapTarget( app, "/composition/speed" );

	// Right side: output state and frame rate.
	float fps = 0;
	if( !app.frameTimes.empty() )
	{
		float sum = 0;
		for( float t : app.frameTimes )
			sum += t;
		fps = sum > 0 ? 1000.0f * app.frameTimes.size() / sum : 0;
	}
	char right[ 160 ];
	int shown = 0;
	for( size_t s = 0; s < app.advancedOutput.screens.size(); ++s )
		if( app.outputs.DeviceStatus( s ).rfind( "virtual", 0 ) != 0 && app.outputs.DeviceStatus( s ).find( "not connected" ) == std::string::npos )
			shown++;
	std::snprintf( right, sizeof( right ), "Output: %d of %d screen%s shown    %.0f fps", shown, (int)app.advancedOutput.screens.size(),
	               app.advancedOutput.screens.size() == 1 ? "" : "s", fps );
	float rightWidth = ImGui::CalcTextSize( right ).x;
	ImGui::SameLine( ImGui::GetWindowWidth() - rightWidth - Ui( 14 ) );
	ImGui::AlignTextToFramePadding();
	ImGui::TextColored( shown ? ImVec4( 0.4f, 0.88f, 0.75f, 1 ) : ImVec4( 0.6f, 0.6f, 0.6f, 1 ), "%s", right );
	ImGui::EndGroup();
	ImGui::SetCursorPosY( ImGui::GetCursorPosY() + Ui( 2 ) );
}

void DrawClipGrid( App& app )
{
	Composition& comp = app.composition;
	if( !ImGui::Begin( panel::Clips, &app.panels.clips ) )
	{
		ImGui::End();
		return;
	}
	const float cellW   = Ui( 104 );
	const float thumbH  = std::floor( cellW * 9 / 16 );
	const float labelH  = ImGui::GetTextLineHeight() + Ui( 4 );
	const float cellH   = thumbH + labelH;
	const float leftW   = Ui( 168 );
	const float gap     = Ui( 2 );
	const float headerH = ImGui::GetFrameHeight() + Ui( 2 );
	ImDrawList* draw    = ImGui::GetWindowDrawList();
	bool dropPending    = !app.droppedPaths.empty();
	ImVec2 dropPoint( app.dropX, app.dropY );
	// Loads may add columns and move clips in memory, so they wait until the grid is drawn.
	struct PendingLoad
	{
		int layer = -1, column = -1;
		std::vector< std::string > paths;
		std::string generatorId;
	} pendingLoad;

	ImGui::BeginChild( "##grid", ImVec2( 0, 0 ), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar );
	draw         = ImGui::GetWindowDrawList();
	ImVec2 origin = ImGui::GetCursorScreenPos();

	// Column headers.
	ImGui::SetCursorScreenPos( origin );
	ImGui::AlignTextToFramePadding();
	ImGui::TextColored( ImVec4( 0.6f, 0.6f, 0.6f, 1 ), "%s", comp.name.c_str() );
	for( int c = 0; c < comp.ColumnCount(); ++c )
	{
		ImVec2 pos( origin.x + leftW + c * ( cellW + gap ), origin.y );
		ImGui::SetCursorScreenPos( pos );
		ImGui::PushID( c );
		bool clicked = ImGui::InvisibleButton( "##column", ImVec2( cellW, headerH - Ui( 2 ) ) );
		bool hovered = ImGui::IsItemHovered();
		bool active  = comp.activeColumn == c;
		ImU32 bg     = active ? theme::Accent : ImGui::GetColorU32( hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button );
		ImVec2 max( pos.x + cellW, pos.y + headerH - Ui( 2 ) );
		draw->AddRectFilled( pos, max, bg, Ui( 2 ) );
		ImU32 fg = active ? IM_COL32( 20, 20, 20, 255 ) : IM_COL32( 220, 220, 220, 255 );
		DrawIcon( draw, Icon::Play, pos, ImVec2( pos.x + max.y - pos.y, max.y ), fg );
		ImVec2 textSize = ImGui::CalcTextSize( comp.columns[ c ].c_str() );
		draw->AddText( ImVec2( pos.x + ( cellW - textSize.x ) / 2 + Ui( 6 ), pos.y + ( max.y - pos.y - textSize.y ) / 2 ), fg, comp.columns[ c ].c_str() );
		if( clicked )
			app.LaunchColumn( c );
		MapTarget( app, "/composition/columns/" + std::to_string( c + 1 ) + "/connect" );
		if( ImGui::BeginPopupContextItem( "##columnMenu" ) )
		{
			ImGui::SetNextItemWidth( Ui( 160 ) );
			ImGui::InputText( "Name", &comp.columns[ c ] );
			ImGui::EndPopup();
		}
		ImGui::PopID();
	}
	{
		ImVec2 pos( origin.x + leftW + comp.ColumnCount() * ( cellW + gap ), origin.y );
		ImGui::SetCursorScreenPos( pos );
		if( ImGui::Button( "+##addColumn", ImVec2( headerH, headerH - Ui( 2 ) ) ) )
			comp.AddColumn();
		if( ImGui::IsItemHovered() )
			ImGui::SetTooltip( "New column" );
	}

	// Layers, top layer first.
	int layerCount = (int)comp.layers.size();
	for( int row = 0; row < layerCount; ++row )
	{
		int l        = layerCount - 1 - row;
		Layer& layer = comp.layers[ l ];
		float y      = origin.y + headerH + row * ( cellH + gap );
		ImGui::PushID( l );

		// Layer strip.
		ImVec2 stripMin( origin.x, y ), stripMax( origin.x + leftW - gap * 2, y + cellH );
		draw->AddRectFilled( stripMin, stripMax, comp.selectedLayer == l ? IM_COL32( 52, 52, 52, 255 ) : IM_COL32( 40, 40, 40, 255 ), Ui( 2 ) );
		if( layer.active >= 0 )
			draw->AddRectFilled( stripMin, ImVec2( stripMin.x + Ui( 3 ), stripMax.y ), theme::Accent );
		ImGui::SetCursorScreenPos( ImVec2( origin.x + Ui( 6 ), y + Ui( 4 ) ) );
		std::string layerAddress = "/composition/layers/" + std::to_string( l + 1 );
		if( IconButton( "##clear", Icon::Close ) )
			app.ClearLayer( l );
		MapTarget( app, layerAddress + "/clear" );
		if( ImGui::IsItemHovered() )
			ImGui::SetTooltip( "Clear layer" );
		ImGui::SameLine();
		if( LetterButton( "bypass", "B", layer.bypass, IM_COL32( 230, 170, 70, 255 ) ) )
			layer.bypass = !layer.bypass;
		MapTarget( app, layerAddress + "/bypassed" );
		if( ImGui::IsItemHovered() )
			ImGui::SetTooltip( "Bypass" );
		ImGui::SameLine();
		if( LetterButton( "solo", "S", layer.solo, theme::Solo ) )
			layer.solo = !layer.solo;
		MapTarget( app, layerAddress + "/solo" );
		if( ImGui::IsItemHovered() )
			ImGui::SetTooltip( "Solo" );
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		if( ImGui::Selectable( layer.name.c_str(), false, 0, ImVec2( std::max( 1.0f, stripMax.x - ImGui::GetCursorScreenPos().x - Ui( 4 ) ), 0 ) ) )
			comp.selectedLayer = l;
		if( AcceptEffectDrop( app, layer.effects ) )
			comp.selectedLayer = l;
		if( !layer.effects.empty() && ImGui::IsItemHovered() )
			ImGui::SetTooltip( "%d effect%s on this layer", (int)layer.effects.size(), layer.effects.size() == 1 ? "" : "s" );
		ImGui::SetCursorScreenPos( ImVec2( origin.x + Ui( 6 ), y + cellH - ImGui::GetFrameHeight() - Ui( 6 ) ) );
		ImGui::SetNextItemWidth( leftW - Ui( 18 ) );
		PercentSlider( "##opacity", &layer.opacity );
		MapTarget( app, layerAddress + "/video/opacity" );

		// Cells.
		for( int c = 0; c < comp.ColumnCount(); ++c )
		{
			Clip& clip = layer.clips[ c ];
			ImVec2 min( origin.x + leftW + c * ( cellW + gap ), y );
			ImVec2 max( min.x + cellW, min.y + cellH );
			ImGui::SetCursorScreenPos( min );
			ImGui::PushID( c );
			bool clicked = ImGui::InvisibleButton( "##cell", ImVec2( cellW, cellH ) );
			bool hovered = ImGui::IsItemHovered();
			ImVec2 cellMin = ImGui::GetItemRectMin(), cellMax = ImGui::GetItemRectMax();
			draw->AddRectFilled( min, max, hovered ? IM_COL32( 40, 40, 40, 255 ) : theme::CellEmpty );
			if( !clip.Empty() )
			{
				ImVec2 thumbMax( max.x, min.y + thumbH );
				GLuint thumbnail = clip.player ? clip.player->Thumbnail().Texture() : clip.generatorThumbnail ? clip.generatorThumbnail->Texture() : 0;
				if( thumbnail )
				{
					if( clip.generator )
						draw->AddRectFilled( min, thumbMax, IM_COL32( 0, 0, 0, 255 ) );
					draw->AddImage( TextureId( thumbnail ), min, thumbMax, ImVec2( 0, 1 ), ImVec2( 1, 0 ) );
				}
				else
					draw->AddRectFilled( min, thumbMax, IM_COL32( 60, 30, 30, 255 ) );
				if( !clip.effects.empty() )
					draw->AddText( ImVec2( thumbMax.x - ImGui::CalcTextSize( "fx" ).x - Ui( 3 ), min.y + Ui( 1 ) ), theme::Accent, "fx" );
				ImU32 labelColour = clip.Playable() ? IM_COL32( 210, 210, 210, 255 ) : theme::Error;
				draw->PushClipRect( ImVec2( min.x, thumbMax.y ), max, true );
				draw->AddText( ImVec2( min.x + Ui( 3 ), thumbMax.y + Ui( 2 ) ), labelColour, clip.name.c_str() );
				draw->PopClipRect();
				if( layer.active == c )
				{
					// Progress along the bottom of a playing clip.
					float progress = clip.Duration() > 0 ? (float)( clip.time / clip.Duration() ) : 0;
					draw->AddRectFilled( ImVec2( min.x, thumbMax.y - Ui( 3 ) ), ImVec2( min.x + cellW * progress, thumbMax.y ), theme::Accent );
				}
				if( hovered && !clip.Playable() && clip.player )
					ImGui::SetTooltip( "%s\n%s", clip.path.c_str(), clip.player->Error().c_str() );
				else if( hovered && clip.IsGenerator() && !clip.generator )
					ImGui::SetTooltip( "Source not installed: %s", clip.generatorId.c_str() );
				else if( hovered && ImGui::GetIO().KeyAlt )
					ImGui::SetTooltip( "%s", clip.path.c_str() );
			}
			if( layer.active == c )
				draw->AddRect( min, max, theme::Accent, 0, 0, Ui( 2 ) );
			else if( comp.selectedLayer == l && comp.selectedColumn == c )
				draw->AddRect( min, max, theme::Selected, 0, 0, Ui( 1 ) );
			else
				draw->AddRect( min, max, theme::CellBorder );

			if( clicked )
			{
				if( clip.Empty() )
					app.Select( l, c );
				else
					app.Launch( l, c );
			}
			if( !clip.Empty() && ImGui::BeginDragDropSource() )
			{
				CellRef ref{ l, c };
				ImGui::SetDragDropPayload( kCellPayload, &ref, sizeof( ref ) );
				ImGui::TextUnformatted( clip.name.c_str() );
				ImGui::EndDragDropSource();
			}
			if( ImGui::BeginDragDropTarget() )
			{
				if( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( kCellPayload ) )
				{
					CellRef ref = *(const CellRef*)payload->Data;
					app.MoveClip( ref.layer, ref.column, l, c );
				}
				if( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( kFilePayload ) )
					pendingLoad = { l, c, { std::string( (const char*)payload->Data ) }, {} };
				if( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( kEffectPayload ) )
				{
					std::string id                = (const char*)payload->Data;
					const EffectRegistry::Entry* e = app.registry->Find( id );
					if( e && e->source )
						pendingLoad = { l, c, {}, id };
					else if( !clip.Empty() )
					{
						app.AddEffect( clip.effects, id );
						app.Select( l, c );
					}
				}
				ImGui::EndDragDropTarget();
			}
			if( ImGui::BeginPopupContextItem( "##cellMenu" ) )
			{
				app.Select( l, c );
				if( ImGui::MenuItem( "Launch", nullptr, false, clip.Playable() ) )
					app.Launch( l, c );
				if( ImGui::MenuItem( "Load Media..." ) )
					ShowFileDialog( FileDialog::OpenMedia, app.browserDirectory,
					                [ &app, l, c ]( const std::vector< std::string >& paths ) { LoadInto( app, l, c, paths ); } );
				if( ImGui::MenuItem( "Remove Clip", nullptr, false, !clip.Empty() ) )
					app.RemoveClip( l, c );
				ImGui::EndPopup();
			}
			if( app.learning )
			{
				// The highlight goes over the drawn cell (the cell button is the item it belongs to).
				ImGui::SetCursorScreenPos( cellMin );
				ImGui::Dummy( ImVec2( cellMax.x - cellMin.x, cellMax.y - cellMin.y ) );
				MapTarget( app, layerAddress + "/clips/" + std::to_string( c + 1 ) + "/connect" );
			}
			if( dropPending && dropPoint.x >= min.x && dropPoint.x < max.x && dropPoint.y >= min.y && dropPoint.y < max.y )
			{
				pendingLoad = { l, c, app.droppedPaths, {} };
				dropPending = false;
			}
			ImGui::PopID();
		}
		ImGui::PopID();
	}

	float bottom = origin.y + headerH + layerCount * ( cellH + gap );
	ImGui::SetCursorScreenPos( ImVec2( origin.x, bottom + Ui( 2 ) ) );
	if( ImGui::Button( "+ Layer" ) )
		comp.AddLayer();
	ImGui::SameLine();
	ImGui::TextDisabled( "Drop HAP clips or sources on cells and effects on clips or layers. Drag a clip to move it, right-click for options." );
	ImGui::Dummy( ImVec2( leftW + comp.ColumnCount() * ( cellW + gap ) + headerH, 0 ) );
	ImGui::EndChild();

	if( pendingLoad.layer >= 0 && !pendingLoad.generatorId.empty() )
		app.LoadGenerator( pendingLoad.layer, pendingLoad.column, pendingLoad.generatorId );
	else if( pendingLoad.layer >= 0 )
		LoadInto( app, pendingLoad.layer, pendingLoad.column, pendingLoad.paths );
	// Dropped anywhere else: fill the selected layer.
	if( dropPending )
		LoadIntoSelected( app, app.droppedPaths );
	ImGui::End();
}

void DrawCompositionMonitor( App& app )
{
	ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0, 0 ) );
	bool open = ImGui::Begin( panel::CompositionMonitor, &app.panels.compositionMonitor );
	ImGui::PopStyleVar();
	if( open )
	{
		Composition& comp = app.composition;
		ImVec2 area       = ImGui::GetContentRegionAvail();
		area.y -= ImGui::GetTextLineHeightWithSpacing();
		ImVec2 offset;
		ImVec2 size = FitSize( area, (float)comp.width / comp.height, &offset );
		ImGui::SetCursorPos( ImVec2( ImGui::GetCursorPosX() + offset.x, ImGui::GetCursorPosY() + offset.y ) );
		FboImage( app.compositor.CompositionTexture(), size );
		AcceptEffectDrop( app, comp.effects );
		ImGui::SetCursorPosX( Ui( 6 ) );
		ImGui::TextDisabled( "Composition - %dx%d", comp.width, comp.height );
	}
	ImGui::End();
}

void DrawPreviewMonitor( App& app )
{
	ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0, 0 ) );
	bool open = ImGui::Begin( panel::PreviewMonitor, &app.panels.previewMonitor );
	ImGui::PopStyleVar();
	if( open )
	{
		Clip* clip  = app.composition.SelectedClip();
		ImVec2 area = ImGui::GetContentRegionAvail();
		area.y -= ImGui::GetTextLineHeightWithSpacing();
		ImVec2 offset;
		ImVec2 size = FitSize( area, 16.0f / 9.0f, &offset );
		ImGui::SetCursorPos( ImVec2( ImGui::GetCursorPosX() + offset.x, ImGui::GetCursorPosY() + offset.y ) );
		if( clip && clip->generator )
		{
			FboImage( app.preview.Texture(), size );
			ImGui::SetCursorPosX( Ui( 6 ) );
			ImGui::TextDisabled( "Preview - %s - source", clip->name.c_str() );
		}
		else if( clip && clip->Loaded() )
		{
			bool live = clip->player->Active() && clip->player->HasFrame();
			FboImage( live ? app.preview.Texture() : clip->player->Thumbnail().Texture(), size );
			ImGui::SetCursorPosX( Ui( 6 ) );
			ImGui::TextDisabled( "Preview - %s - %dx%d%s", clip->name.c_str(), clip->player->Width(), clip->player->Height(),
			                     live ? "" : " (thumbnail)" );
		}
		else
		{
			ImGui::Dummy( size );
			ImGui::SetCursorPosX( Ui( 6 ) );
			ImGui::TextDisabled( "Preview - select a clip" );
		}
	}
	ImGui::End();
}

void DrawCompositionPanel( App& app )
{
	if( !ImGui::Begin( panel::Composition, &app.panels.composition ) )
	{
		ImGui::End();
		return;
	}
	Composition& comp = app.composition;
	ImGui::SeparatorText( comp.name.c_str() );
	if( BeginParams( "##comp" ) )
	{
		ParamLabel( "Name" );
		ImGui::InputText( "##name", &comp.name );
		ParamLabel( "Master" );
		PercentSlider( "##master", &comp.master );
		MapTarget( app, "/composition/master" );
		ParamLabel( "Speed" );
		ImGui::SliderFloat( "##speed", &comp.speed, 0.0f, 4.0f, "%.2f" );
		MapTarget( app, "/composition/speed" );
		ParamLabel( "BPM" );
		float bpm = (float)comp.bpm;
		if( ImGui::DragFloat( "##bpm", &bpm, 0.1f, 20.0f, 500.0f, "%.2f" ) )
			comp.bpm = bpm;
		MapTarget( app, "/composition/tempocontroller/tempo" );
		ParamLabel( "Size" );
		struct Preset
		{
			const char* name;
			int w, h;
		};
		const Preset presets[] = { { "1280 x 720", 1280, 720 },     { "1920 x 1080", 1920, 1080 }, { "2560 x 1440", 2560, 1440 },
			                       { "3840 x 2160", 3840, 2160 },   { "1080 x 1920", 1080, 1920 }, { "1024 x 768", 1024, 768 } };
		char current[ 32 ];
		std::snprintf( current, sizeof( current ), "%d x %d", comp.width, comp.height );
		if( ImGui::BeginCombo( "##size", current ) )
		{
			for( const Preset& p : presets )
				if( ImGui::Selectable( p.name, comp.width == p.w && comp.height == p.h ) )
				{
					comp.width  = p.w;
					comp.height = p.h;
				}
			ImGui::EndCombo();
		}
		ParamLabel( "Custom" );
		int size[ 2 ] = { comp.width, comp.height };
		if( ImGui::InputInt2( "##custom", size, ImGuiInputTextFlags_EnterReturnsTrue ) )
		{
			comp.width  = std::clamp( size[ 0 ], 16, 16384 );
			comp.height = std::clamp( size[ 1 ], 16, 16384 );
		}
		ImGui::EndTable();
	}
	if( ImGui::CollapsingHeader( "Effects", ImGuiTreeNodeFlags_DefaultOpen ) )
		DrawEffectChain( app, comp.effects, "compositionFx", "/composition" );
	ImGui::End();
}

void DrawLayerPanel( App& app )
{
	if( !ImGui::Begin( panel::Layer, &app.panels.layer ) )
	{
		ImGui::End();
		return;
	}
	Composition& comp = app.composition;
	if( comp.layers.empty() )
	{
		ImGui::TextDisabled( "No layers" );
		ImGui::End();
		return;
	}
	int l        = std::clamp( comp.selectedLayer, 0, (int)comp.layers.size() - 1 );
	Layer& layer = comp.layers[ l ];
	ImGui::SeparatorText( layer.name.c_str() );
	if( BeginParams( "##layer" ) )
	{
		ParamLabel( "Name" );
		ImGui::InputText( "##name", &layer.name );
		std::string layerAddress = "/composition/layers/" + std::to_string( l + 1 );
		ParamLabel( "Opacity" );
		PercentSlider( "##opacity", &layer.opacity );
		MapTarget( app, layerAddress + "/video/opacity" );
		ParamLabel( "Blend Mode" );
		EnumCombo( "##blend", &layer.blend, BlendModeName );
		ParamLabel( "Bypass" );
		ImGui::Checkbox( "##bypass", &layer.bypass );
		MapTarget( app, layerAddress + "/bypassed" );
		ParamLabel( "Solo" );
		ImGui::Checkbox( "##solo", &layer.solo );
		MapTarget( app, layerAddress + "/solo" );
		ImGui::EndTable();
	}
	if( ImGui::Button( "Clear Layer" ) )
		app.ClearLayer( l );
	MapTarget( app, "/composition/layers/" + std::to_string( l + 1 ) + "/clear" );
	ImGui::SameLine();
	if( ImGui::Button( "Delete Layer" ) && comp.layers.size() > 1 )
	{
		app.ClearLayer( l );
		comp.layers.erase( comp.layers.begin() + l );
		comp.Normalise();
		ImGui::End();
		return;
	}
	if( ImGui::CollapsingHeader( "Effects", ImGuiTreeNodeFlags_DefaultOpen ) )
		DrawEffectChain( app, layer.effects, "layerFx", "/composition/layers/" + std::to_string( l + 1 ) );
	ImGui::End();
}

void DrawClipPanel( App& app )
{
	if( !ImGui::Begin( panel::Clip, &app.panels.clip ) )
	{
		ImGui::End();
		return;
	}
	Clip* clip = app.composition.SelectedClip();
	if( !clip )
	{
		ImGui::TextDisabled( "Select a clip in the deck." );
		ImGui::End();
		return;
	}
	ImGui::SeparatorText( clip->name.c_str() );
	if( clip->IsGenerator() )
	{
		if( !clip->generator )
		{
			ImGui::TextColored( ImVec4( 0.9f, 0.35f, 0.3f, 1 ), "Source not installed: %s", clip->generatorId.c_str() );
			ImGui::End();
			return;
		}
		ImGui::TextDisabled( "Source, rendered at the composition size" );
		if( ImGui::CollapsingHeader( "Source", ImGuiTreeNodeFlags_DefaultOpen ) )
		{
			ImGui::PushID( "source" );
			DrawEffectParams( *clip->generator );
			ImGui::PopID();
		}
	}
	else if( !clip->Loaded() )
	{
		ImGui::TextColored( ImVec4( 0.9f, 0.35f, 0.3f, 1 ), "%s", clip->player ? clip->player->Error().c_str() : "Not loaded" );
		ImGui::TextWrapped( "%s", clip->path.c_str() );
		ImGui::End();
		return;
	}
	else
	{
		const FrameSource& source = clip->player->Source();
		if( source.IsStill() )
			ImGui::TextDisabled( "%s  %dx%d  still image", source.CodecName().c_str(), clip->player->Width(), clip->player->Height() );
		else
			ImGui::TextDisabled( "%s  %dx%d  %.2f fps  %zu frames  %s", source.CodecName().c_str(), clip->player->Width(), clip->player->Height(),
			                     source.FrameRate(), source.FrameCount(), source.Details().c_str() );
		if( !source.IsHap() && !source.IsStill() )
			ImGui::TextColored( ImVec4( 0.95f, 0.7f, 0.3f, 1 ), "Not HAP: decoded on the CPU. Convert to HAP for the best performance." );
	}

	if( clip->Loaded() && !clip->player->Source().IsStill() && ImGui::CollapsingHeader( "Transport", ImGuiTreeNodeFlags_DefaultOpen ) )
	{
		if( IconButton( "##reverse", Icon::Reverse, clip->direction < 0 ) )
			clip->direction = -1;
		ImGui::SameLine();
		if( IconButton( "##pause", Icon::Pause, clip->direction == 0 ) )
			clip->direction = 0;
		ImGui::SameLine();
		if( IconButton( "##forward", Icon::Play, clip->direction > 0 ) )
		{
			clip->direction = 1;
			clip->finished  = false;
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth( Ui( 110 ) );
		EnumCombo( "##playMode", &clip->playMode, PlayModeName );

		float time     = (float)clip->time;
		float duration = (float)clip->Duration();
		std::string label = FormatTime( clip->time ) + " / " + FormatTime( duration ) + "   frame " + std::to_string( clip->CurrentFrame() );
		ImGui::SetNextItemWidth( -FLT_MIN );
		if( ImGui::SliderFloat( "##time", &time, 0.0f, duration, label.c_str() ) )
		{
			clip->time     = std::clamp( (double)time, 0.0, (double)duration );
			clip->finished = false;
		}
		if( BeginParams( "##transport" ) )
		{
			ParamLabel( "Speed" );
			ImGui::SliderFloat( "##speed", &clip->speed, 0.0f, 4.0f, "%.2f" );
			if( ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
				clip->speed = 1.0f;
			ParamLabel( "Duration" );
			ImGui::Text( "%s", FormatTime( duration ).c_str() );
			ImGui::EndTable();
		}
	}
	if( ImGui::CollapsingHeader( "Video", ImGuiTreeNodeFlags_DefaultOpen ) && BeginParams( "##video" ) )
	{
		ParamLabel( "Opacity" );
		PercentSlider( "##opacity", &clip->opacity );
		ImGui::EndTable();
	}
	if( ImGui::CollapsingHeader( "Transform", ImGuiTreeNodeFlags_DefaultOpen ) && BeginParams( "##transform" ) )
	{
		if( !clip->generator )
		{
			ParamLabel( "Fit" );
			EnumCombo( "##fit", &clip->fit, FitModeName );
		}
		ParamLabel( "Position X" );
		ImGui::DragFloat( "##x", &clip->positionX, 1.0f, -16384, 16384, "%.0f" );
		ParamLabel( "Position Y" );
		ImGui::DragFloat( "##y", &clip->positionY, 1.0f, -16384, 16384, "%.0f" );
		ParamLabel( "Scale" );
		float scale = clip->scale * 100;
		if( ImGui::DragFloat( "##scale", &scale, 0.5f, 0, 1000, "%.1f %%" ) )
			clip->scale = scale / 100;
		ParamLabel( "Rotation" );
		ImGui::SliderFloat( "##rotation", &clip->rotation, -180, 180, "%.1f deg" );
		ParamLabel( "" );
		if( ImGui::Button( "Reset Transform" ) )
		{
			clip->positionX = clip->positionY = clip->rotation = 0;
			clip->scale                                        = 1;
		}
		ImGui::EndTable();
	}
	if( ImGui::CollapsingHeader( "Effects", ImGuiTreeNodeFlags_DefaultOpen ) )
		DrawEffectChain( app, clip->effects, "clipFx",
		                 "/composition/layers/" + std::to_string( app.composition.selectedLayer + 1 ) + "/clips/" + std::to_string( app.composition.selectedColumn + 1 ) );
	if( clip->Loaded() && ImGui::CollapsingHeader( "Playback Health" ) && BeginParams( "##health" ) )
	{
		ClipPlayer::Stats stats = clip->player->GetStats();
		ParamLabel( "Decode" );
		ImGui::Text( "%.2f ms/frame", stats.decodeMs );
		ParamLabel( "Upload" );
		ImGui::Text( "%.3f ms/frame", stats.uploadMs );
		ParamLabel( "Late frames" );
		ImGui::Text( "%llu", (unsigned long long)stats.late );
		ImGui::EndTable();
	}
	ImGui::Spacing();
	if( !clip->path.empty() )
		ImGui::TextDisabled( "%s", clip->path.c_str() );
	ImGui::End();
}

void DrawFilesPanel( App& app )
{
	if( !ImGui::Begin( panel::Files, &app.panels.files ) )
	{
		ImGui::End();
		return;
	}
	fs::path dir = fs::u8path( app.browserDirectory );
	if( ImGui::ArrowButton( "##up", ImGuiDir_Up ) && dir.has_parent_path() && dir.parent_path() != dir )
		app.browserDirectory = dir.parent_path().u8string();
	ImGui::SameLine();
	if( ImGui::Button( "Home" ) )
		app.browserDirectory = HomeDirectory();
	ImGui::SameLine();
	if( ImGui::Button( "Browse..." ) )
		ShowFileDialog( FileDialog::PickFolder, app.browserDirectory, [ &app ]( const std::vector< std::string >& paths ) { app.browserDirectory = paths[ 0 ]; } );
	ImGui::SameLine();
	ImGui::SetNextItemWidth( -FLT_MIN );
	std::string edit = app.browserDirectory;
	if( ImGui::InputText( "##dir", &edit, ImGuiInputTextFlags_EnterReturnsTrue ) )
	{
		std::error_code ignored;
		if( fs::is_directory( fs::u8path( edit ), ignored ) )
			app.browserDirectory = edit;
	}

	const Listing& listing = ListDirectory( app.browserDirectory );
	ImGui::BeginChild( "##list" );
	std::string navigate;
	for( const fs::path& folder : listing.folders )
	{
		std::string label = "[ ] " + folder.filename().u8string();
		if( ImGui::Selectable( label.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick ) && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
			navigate = folder.u8string();
	}
	for( const fs::path& file : listing.files )
	{
		std::string name = file.filename().u8string();
		bool composition = IsComposition( file );
		if( composition )
			ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 0.4f, 0.88f, 0.75f, 1 ) );
		if( ImGui::Selectable( name.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick ) && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
		{
			if( composition )
				app.OpenComposition( file.u8string() );
			else
				LoadIntoSelected( app, { file.u8string() } );
		}
		if( composition )
			ImGui::PopStyleColor();
		if( !composition && ImGui::BeginDragDropSource() )
		{
			std::string path = file.u8string();
			ImGui::SetDragDropPayload( kFilePayload, path.c_str(), path.size() + 1 );
			ImGui::TextUnformatted( name.c_str() );
			ImGui::EndDragDropSource();
		}
	}
	if( listing.folders.empty() && listing.files.empty() )
		ImGui::TextDisabled( "No folders or clips here." );
	ImGui::EndChild();
	if( !navigate.empty() )
		app.browserDirectory = navigate;
	ImGui::End();
}

void DrawOutputPanel( App& app )
{
	if( !ImGui::Begin( panel::Output, &app.panels.output ) )
	{
		ImGui::End();
		return;
	}
	if( ImGui::Button( "Advanced Output...", ImVec2( -FLT_MIN, 0 ) ) )
		app.panels.advancedOutput = true;
	std::vector< OutputWindow::Display > displays = OutputWindow::Displays();
	for( size_t s = 0; s < app.advancedOutput.screens.size(); ++s )
	{
		Screen& screen = app.advancedOutput.screens[ s ];
		ImGui::PushID( (int)s );
		ImGui::SeparatorText( screen.name.c_str() );
		std::string current = screen.device == OutputDevice::Display ? screen.display : OutputDeviceName( screen.device );
		ImGui::SetNextItemWidth( -FLT_MIN );
		if( ImGui::BeginCombo( "##device", current.c_str() ) )
		{
			if( ImGui::Selectable( "Virtual Output (not shown)", screen.device == OutputDevice::Virtual ) )
				screen.device = OutputDevice::Virtual;
			if( ImGui::Selectable( "Window", screen.device == OutputDevice::Window ) )
				app.ShowScreenOn( s, OutputDevice::Window );
			for( const OutputWindow::Display& d : displays )
			{
				std::string label = "Fullscreen " + d.name + "  " + std::to_string( d.width ) + "x" + std::to_string( d.height ) + " @ " + std::to_string( d.refresh ) + " Hz";
				if( ImGui::Selectable( label.c_str(), screen.device == OutputDevice::Display && screen.display == d.monitorName ) )
					app.ShowScreenOn( s, OutputDevice::Display, d.monitorName );
			}
			ImGui::EndCombo();
		}
		ImGui::TextDisabled( "%dx%d, %d slice%s - %s", screen.width, screen.height, (int)screen.slices.size(), screen.slices.size() == 1 ? "" : "s",
		                     app.outputs.DeviceStatus( s ).c_str() );
		ImGui::PopID();
	}
	ImGui::Spacing();
	if( ImGui::Button( "Stop All Outputs", ImVec2( -FLT_MIN, 0 ) ) )
		app.StopOutputs();
	ImGui::End();
}

void DrawPerformancePanel( App& app )
{
	if( !ImGui::Begin( panel::Performance, &app.panels.performance ) )
	{
		ImGui::End();
		return;
	}
	std::vector< float > times( app.frameTimes.begin(), app.frameTimes.end() );
	float average = 0, worst = 0;
	for( float t : times )
	{
		average += t;
		worst = std::max( worst, t );
	}
	average = times.empty() ? 0 : average / times.size();
	ImGui::Text( "%.1f fps   %.2f ms average   %.2f ms worst", average > 0 ? 1000 / average : 0, average, worst );
	ImGui::PlotLines( "##frames", times.data(), (int)times.size(), 0, nullptr, 0, std::max( 40.0f, worst ), ImVec2( -FLT_MIN, Ui( 50 ) ) );
	ImGui::TextDisabled( "GPU: %s", Caps().renderer );
	ImGui::TextDisabled( "OpenGL %s   DXT %s   BPTC %s   decode threads %u", Caps().version, Caps().s3tc ? "yes" : "NO", Caps().bptc ? "yes" : "no",
	                     ThreadPool::Shared().WorkerCount() + 1 );

	if( ImGui::BeginTable( "##layers", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp ) )
	{
		ImGui::TableSetupColumn( "Layer" );
		ImGui::TableSetupColumn( "Clip" );
		ImGui::TableSetupColumn( "Format" );
		ImGui::TableSetupColumn( "Decode ms" );
		ImGui::TableSetupColumn( "Upload ms" );
		ImGui::TableSetupColumn( "Late" );
		ImGui::TableHeadersRow();
		for( int l = (int)app.composition.layers.size() - 1; l >= 0; --l )
		{
			Layer& layer = app.composition.layers[ l ];
			if( layer.active < 0 || !layer.clips[ layer.active ].Loaded() )
				continue;
			Clip& clip              = layer.clips[ layer.active ];
			ClipPlayer::Stats stats = clip.player->GetStats();
			ImGui::TableNextRow();
			ImGui::TableNextColumn();
			ImGui::TextUnformatted( layer.name.c_str() );
			ImGui::TableNextColumn();
			ImGui::TextUnformatted( clip.name.c_str() );
			ImGui::TableNextColumn();
			ImGui::Text( "%s %dx%d", clip.player->Source().CodecName().c_str(), clip.player->Width(), clip.player->Height() );
			ImGui::TableNextColumn();
			ImGui::Text( "%.2f", stats.decodeMs );
			ImGui::TableNextColumn();
			ImGui::Text( "%.3f", stats.uploadMs );
			ImGui::TableNextColumn();
			ImGui::Text( "%llu", (unsigned long long)stats.late );
		}
		ImGui::EndTable();
	}
	ImGui::End();
}

void DrawStatusBar( App& app )
{
	ImGui::SetCursorPosX( Ui( 8 ) );
	ImGui::AlignTextToFramePadding();
	bool fresh = glfwGetTime() - app.statusTime < 8.0;
	if( fresh && !app.status.empty() )
		ImGui::TextColored( app.statusIsError ? ImVec4( 0.9f, 0.35f, 0.3f, 1 ) : ImVec4( 0.7f, 0.7f, 0.7f, 1 ), "%s", app.status.c_str() );
	else
		ImGui::TextDisabled( "%s", app.compositionPath.empty() ? "Unsaved composition" : app.compositionPath.c_str() );
	const char* version = "STLT-VJ 0.1";
	ImGui::SameLine( ImGui::GetWindowWidth() - ImGui::CalcTextSize( version ).x - Ui( 12 ) );
	ImGui::TextDisabled( "%s", version );
}
}
