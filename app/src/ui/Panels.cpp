#include "ui/Panels.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_internal.h> // GetItemFlags: whether the last item is disabled
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
#include "ui/AdvancedOutputUi.h"
#include "ui/ControlUi.h"
#include "ui/EffectsUi.h"
#include "ui/FileDialogs.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace fs = std::filesystem;

namespace stltvj
{
namespace
{
constexpr const char* kCellPayload = "STLT_CELL";
constexpr const char* kMovePayload = "STLT_MOVE"; // the picked clips, dragged in Move mode
constexpr ImU32 kMoveColour        = IM_COL32( 214, 120, 255, 255 );
constexpr const char* kFilePayload = "STLT_FILE";

struct CellRef
{
	int layer, column;
};

float Ui( float pixels )
{
	return pixels * ImGui::GetFontSize() / theme::BaseFont;
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

bool IsAudioFile( const fs::path& path )
{
	return IsAudioExtension( path.extension().u8string() );
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

bool PercentSlider( const char* id, float* value, float max = 1.0f, const char* format = "%.0f %%" )
{
	float percent = *value * 100.0f;
	bool changed  = FilledSlider( id, &percent, 0.0f, max * 100.0f, format );
	if( changed )
		*value = percent / 100.0f;
	changed |= ResetValue( value, 1.0f );
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
	float r    = std::min( max.x - min.x, max.y - min.y ) * 0.28f;
	float line = Ui( 1.5f );
	switch( icon )
	{
	case Icon::Play: draw->AddTriangleFilled( ImVec2( c.x - r * 0.8f, c.y - r ), ImVec2( c.x - r * 0.8f, c.y + r ), ImVec2( c.x + r, c.y ), colour ); break;
	case Icon::Reverse:
		draw->AddTriangleFilled( ImVec2( c.x + r * 0.8f, c.y - r ), ImVec2( c.x + r * 0.8f, c.y + r ), ImVec2( c.x - r, c.y ), colour );
		break;
	case Icon::Pause:
		draw->AddRectFilled( ImVec2( c.x - r * 0.8f, c.y - r ), ImVec2( c.x - r * 0.22f, c.y + r ), colour, r * 0.18f );
		draw->AddRectFilled( ImVec2( c.x + r * 0.22f, c.y - r ), ImVec2( c.x + r * 0.8f, c.y + r ), colour, r * 0.18f );
		break;
	case Icon::Stop: draw->AddRectFilled( ImVec2( c.x - r * 0.8f, c.y - r * 0.8f ), ImVec2( c.x + r * 0.8f, c.y + r * 0.8f ), colour, r * 0.22f ); break;
	case Icon::Close:
		draw->AddLine( ImVec2( c.x - r * 0.7f, c.y - r * 0.7f ), ImVec2( c.x + r * 0.7f, c.y + r * 0.7f ), colour, Ui( 1.6f ) );
		draw->AddLine( ImVec2( c.x - r * 0.7f, c.y + r * 0.7f ), ImVec2( c.x + r * 0.7f, c.y - r * 0.7f ), colour, Ui( 1.6f ) );
		break;
	case Icon::Previous:
		draw->AddRectFilled( ImVec2( c.x - r * 0.9f, c.y - r ), ImVec2( c.x - r * 0.55f, c.y + r ), colour );
		draw->AddTriangleFilled( ImVec2( c.x + r * 0.8f, c.y - r ), ImVec2( c.x + r * 0.8f, c.y + r ), ImVec2( c.x - r * 0.45f, c.y ), colour );
		break;
	case Icon::Next:
		draw->AddRectFilled( ImVec2( c.x + r * 0.55f, c.y - r ), ImVec2( c.x + r * 0.9f, c.y + r ), colour );
		draw->AddTriangleFilled( ImVec2( c.x - r * 0.8f, c.y - r ), ImVec2( c.x - r * 0.8f, c.y + r ), ImVec2( c.x + r * 0.45f, c.y ), colour );
		break;
	case Icon::Eye:
	case Icon::EyeOff:
	{
		// A lens made of two arcs, and the pupil; crossed out when off.
		float w = r * 1.25f, h = r * 0.72f, rho = ( w * w + h * h ) / ( 2 * h ), d = rho - h, phi = std::atan2( d, w );
		draw->PathArcTo( ImVec2( c.x, c.y + d ), rho, IM_PI + phi, 2 * IM_PI - phi, 12 );
		draw->PathStroke( colour, 0, line );
		draw->PathArcTo( ImVec2( c.x, c.y - d ), rho, phi, IM_PI - phi, 12 );
		draw->PathStroke( colour, 0, line );
		draw->AddCircleFilled( c, r * 0.36f, colour );
		if( icon == Icon::EyeOff )
			draw->AddLine( ImVec2( c.x - r * 1.05f, c.y + r * 1.05f ), ImVec2( c.x + r * 1.05f, c.y - r * 1.05f ), colour, line );
		break;
	}
	case Icon::Loop:
	{
		draw->PathArcTo( c, r * 0.9f, IM_PI * 0.15f, IM_PI * 1.75f, 16 );
		draw->PathStroke( colour, 0, line );
		ImVec2 e( c.x + r * 0.9f * std::cos( IM_PI * 1.75f ), c.y + r * 0.9f * std::sin( IM_PI * 1.75f ) );
		draw->AddTriangleFilled( ImVec2( e.x - r * 0.45f, e.y - r * 0.35f ), ImVec2( e.x + r * 0.5f, e.y - r * 0.15f ), ImVec2( e.x + r * 0.05f, e.y + r * 0.55f ), colour );
		break;
	}
	case Icon::Bounce:
		draw->AddLine( ImVec2( c.x - r * 0.7f, c.y ), ImVec2( c.x + r * 0.7f, c.y ), colour, line );
		draw->AddTriangleFilled( ImVec2( c.x - r * 1.1f, c.y ), ImVec2( c.x - r * 0.55f, c.y - r * 0.5f ), ImVec2( c.x - r * 0.55f, c.y + r * 0.5f ), colour );
		draw->AddTriangleFilled( ImVec2( c.x + r * 1.1f, c.y ), ImVec2( c.x + r * 0.55f, c.y + r * 0.5f ), ImVec2( c.x + r * 0.55f, c.y - r * 0.5f ), colour );
		break;
	case Icon::Hold:
	case Icon::OnceClear:
		draw->AddLine( ImVec2( c.x - r * 1.1f, c.y ), ImVec2( c.x + r * 0.1f, c.y ), colour, line );
		draw->AddTriangleFilled( ImVec2( c.x + r * 0.55f, c.y ), ImVec2( c.x, c.y + r * 0.5f ), ImVec2( c.x, c.y - r * 0.5f ), colour );
		if( icon == Icon::Hold )
			draw->AddRectFilled( ImVec2( c.x + r * 0.8f, c.y - r ), ImVec2( c.x + r * 1.1f, c.y + r ), colour );
		else
		{
			draw->AddLine( ImVec2( c.x + r * 0.75f, c.y - r * 0.45f ), ImVec2( c.x + r * 1.25f, c.y + r * 0.45f ), colour, line );
			draw->AddLine( ImVec2( c.x + r * 0.75f, c.y + r * 0.45f ), ImVec2( c.x + r * 1.25f, c.y - r * 0.45f ), colour, line );
		}
		break;
	case Icon::Clock:
		draw->AddCircle( c, r * 1.0f, colour, 16, line );
		draw->AddLine( c, ImVec2( c.x, c.y - r * 0.62f ), colour, line );
		draw->AddLine( c, ImVec2( c.x + r * 0.45f, c.y + r * 0.3f ), colour, line );
		break;
	case Icon::Note:
		draw->AddCircleFilled( ImVec2( c.x - r * 0.35f, c.y + r * 0.62f ), r * 0.42f, colour );
		draw->AddLine( ImVec2( c.x + r * 0.05f, c.y + r * 0.6f ), ImVec2( c.x + r * 0.05f, c.y - r * 1.05f ), colour, line );
		draw->AddLine( ImVec2( c.x + r * 0.05f, c.y - r * 1.05f ), ImVec2( c.x + r * 0.75f, c.y - r * 0.45f ), colour, line );
		break;
	case Icon::CaretDown:
		draw->AddTriangleFilled( ImVec2( c.x - r * 0.75f, c.y - r * 0.35f ), ImVec2( c.x + r * 0.75f, c.y - r * 0.35f ), ImVec2( c.x, c.y + r * 0.45f ), colour );
		break;
	case Icon::Plus:
		draw->AddLine( ImVec2( c.x - r * 0.85f, c.y ), ImVec2( c.x + r * 0.85f, c.y ), colour, line );
		draw->AddLine( ImVec2( c.x, c.y - r * 0.85f ), ImVec2( c.x, c.y + r * 0.85f ), colour, line );
		break;
	case Icon::Grid:
	{
		float s = r * 0.85f, g = r * 0.2f;
		for( int i = 0; i < 4; ++i )
		{
			float x = i % 2 ? c.x + g : c.x - g - s, y = i / 2 ? c.y + g : c.y - g - s;
			draw->AddRectFilled( ImVec2( x, y ), ImVec2( x + s, y + s ), colour );
		}
		break;
	}
	case Icon::Lines:
		draw->AddRectFilled( ImVec2( c.x - r, c.y - r ), ImVec2( c.x + r, c.y - r * 0.6f ), colour );
		draw->AddRectFilled( ImVec2( c.x - r, c.y - r * 0.2f ), ImVec2( c.x + r * 0.35f, c.y + r * 0.2f ), colour );
		draw->AddRectFilled( ImVec2( c.x - r, c.y + r * 0.6f ), ImVec2( c.x + r * 0.7f, c.y + r ), colour );
		break;
	}
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
		else if( IsMediaFile( entry.path() ) || IsAudioFile( entry.path() ) || IsComposition( entry.path() ) )
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
		if( IsAudioFile( fs::u8path( path ) ) )
		{
			app.SetStatus( "Sound files go on the timeline's audio tracks (Arrangement view, Tab)", true );
			continue;
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
	// Compositions go to ~/STLT; while only the folder of STLT-VJ (the program's old name) is there, to that one.
	fs::path folder = fs::u8path( HomeDirectory() ) / "STLT";
	std::error_code ignored;
	if( !fs::exists( folder, ignored ) && fs::is_directory( fs::u8path( HomeDirectory() ) / "STLT-VJ", ignored ) )
		folder = fs::u8path( HomeDirectory() ) / "STLT-VJ";
	return ( folder / fs::u8path( app.composition.name + ".stltvj" ) ).u8string();
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
	ImU32 bg         = active ? theme::Accent : hovered ? theme::RaisedHover : theme::Raised;
	ImVec2 max( pos.x + size * 1.3f, pos.y + size );
	draw->AddRectFilled( pos, max, bg );
	DrawIcon( draw, icon, pos, max, active ? theme::OnAccent : hovered ? theme::Text : theme::TextSoft );
	return clicked;
}

// The output modes, as in Resolume's Output menu: one of them is on.
static void DrawOutputModeItems( App& app )
{
	if( ImGui::MenuItem( "Off", nullptr, app.outputMode == OutputMode::Off ) )
		app.SetOutputMode( OutputMode::Off );
	if( ImGui::MenuItem( "Window", nullptr, app.outputMode == OutputMode::Window ) )
		app.SetOutputMode( OutputMode::Window );
	if( ImGui::BeginMenu( "Fullscreen", true ) )
	{
		for( const OutputWindow::Display& d : OutputWindow::Displays() )
		{
			std::string label = d.name + "  (" + std::to_string( d.width ) + "x" + std::to_string( d.height ) + ")";
			if( ImGui::MenuItem( label.c_str(), nullptr, app.outputMode == OutputMode::Fullscreen && OutputWindow::FindDisplay( app.outputDisplay ) == d.monitor ) )
				app.SetOutputMode( OutputMode::Fullscreen, d.id );
		}
		ImGui::EndMenu();
	}
	// Advanced is a mode like the others: chosen, the Advanced Output's screens are on show (chosen again, back to the
	// mode before). Its settings open apart, without touching what is on show. Under them STLT-CHASER's editor, for
	// sequences on the Advanced Output's slices.
	if( ImGui::MenuItem( "Advanced", nullptr, app.outputMode == OutputMode::Advanced ) )
		app.ToggleAdvancedOutput();
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( app.outputMode == OutputMode::Advanced ? "Advanced is on: choose it again to go back to the output before it"
		                                                          : "Show the Advanced Output's screens (slices, warps, masks)" );
	ImGui::Separator();
	if( ImGui::MenuItem( "Advanced Output Settings...", "Ctrl+Shift+A", app.panels.advancedOutput ) )
		app.panels.advancedOutput = true;
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "Opens the Advanced Output editor: screens, slices, warps, masks.\nWhat is on show stays as it is." );
	if( ImGui::MenuItem( "Chaser..." ) )
		app.OpenChaser();
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "Opens STLT-CHASER on the Advanced Output's slices: draw step sequences there, then play them with the\n"
		                   "STLT-CHASER, STLT-WIPER and STLT-OUTLINER effects (Effects panel) on the composition, a layer or a clip" );
}

static void SetBpmSync( Clip& clip, bool on, double bpm );

// The Clip menu, as in Resolume: the selected clip's transport, play mode, trigger, column trigger and size;
// copy, cut, paste, its effects, rename, clear; and a size for every clip at once.
static void DrawClipMenu( App& app )
{
	Composition& comp = app.composition;
	Clip* clip        = comp.SelectedClip();
	static std::string renameText;
	ImGui::TextDisabled( "%s", clip ? clip->name.c_str() : "No clip selected" );
	ImGui::BeginDisabled( !clip );
	// A disabled menu still opens on hover: EndMenu each one that opened, and fill it only with a clip.
	if( ImGui::BeginMenu( "Transport" ) )
	{
		if( clip )
		{
			if( ImGui::MenuItem( "Timeline", nullptr, clip->syncBeats <= 0 ) )
				SetBpmSync( *clip, false, comp.bpm );
			if( ImGui::MenuItem( "BPM Sync", nullptr, clip->syncBeats > 0 ) )
				SetBpmSync( *clip, true, comp.bpm );
		}
		ImGui::EndMenu();
	}
	if( ImGui::BeginMenu( "Play Mode" ) )
	{
		if( clip )
		{
			for( int m = 0; m < (int)PlayMode::Count; ++m )
				if( ImGui::MenuItem( PlayModeName( (PlayMode)m ), nullptr, (int)clip->playMode == m ) )
					clip->playMode = (PlayMode)m;
		}
		ImGui::EndMenu();
	}
	if( ImGui::BeginMenu( "Trigger Style" ) )
	{
		if( clip )
		{
			for( int s = 0; s < (int)TriggerStyle::Count; ++s )
				if( ImGui::MenuItem( TriggerStyleName( (TriggerStyle)s ), nullptr, (int)clip->trigger == s ) )
					clip->trigger = (TriggerStyle)s;
		}
		ImGui::EndMenu();
	}
	if( clip )
		ImGui::MenuItem( "Ignore Column Trigger", nullptr, &clip->ignoreColumn );
	else
		ImGui::MenuItem( "Ignore Column Trigger" );
	const std::pair< FitMode, const char* > sizes[] = { { FitMode::Fill, "Fill" }, { FitMode::Fit, "Fit" }, { FitMode::Stretch, "Stretch" }, { FitMode::Native, "Original" } };
	if( ImGui::BeginMenu( "Resize" ) )
	{
		if( clip )
		{
			for( auto [ fit, name ] : sizes )
				if( ImGui::MenuItem( name, nullptr, clip->fit == fit ) )
					clip->fit = fit;
		}
		ImGui::EndMenu();
	}
	ImGui::Separator();
	if( ImGui::MenuItem( "Cut", "Ctrl+X" ) )
		app.CopyClip( true );
	if( ImGui::MenuItem( "Copy", "Ctrl+C" ) )
		app.CopyClip( false );
	ImGui::EndDisabled();
	bool cell = comp.selectedColumn >= 0 && comp.selectedRow >= 0;
	if( ImGui::MenuItem( "Paste", "Ctrl+V", false, app.HasClipboardClip() && cell ) )
		app.PasteClip();
	ImGui::BeginDisabled( !clip );
	if( ImGui::MenuItem( "Copy Effects", nullptr, false, clip && !clip->effects.empty() ) )
		app.CopyEffects();
	ImGui::EndDisabled();
	if( ImGui::MenuItem( "Paste Effects", nullptr, false, clip && app.HasClipboardEffects() ) )
		app.PasteEffects();
	ImGui::BeginDisabled( !clip );
	if( ImGui::BeginMenu( "Rename" ) )
	{
		if( clip )
		{
			if( ImGui::IsWindowAppearing() )
			{
				renameText = clip->name;
				ImGui::SetKeyboardFocusHere();
			}
			ImGui::SetNextItemWidth( Ui( 200 ) );
			if( ImGui::InputText( "##rename", &renameText, ImGuiInputTextFlags_EnterReturnsTrue ) && !renameText.empty() )
			{
				clip->name = renameText;
				ImGui::CloseCurrentPopup();
			}
		}
		ImGui::EndMenu();
	}
	ImGui::Separator();
	if( ImGui::MenuItem( "Clear" ) )
		app.RemoveClip( comp.selectedRow, comp.selectedColumn );
	ImGui::EndDisabled();
	ImGui::Separator();
	if( ImGui::BeginMenu( "Resize Every Clip" ) )
	{
		for( auto [ fit, name ] : sizes )
			if( ImGui::MenuItem( name ) )
			{
				int count = app.ResizeAllClips( fit );
				app.SetStatus( "Resized " + std::to_string( count ) + " clips to " + name );
			}
		ImGui::EndMenu();
	}
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "Every clip of every deck, and the timeline's. New clips: Edit > Preferences > Defaults." );
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
			ImGui::Separator();
			if( ImGui::MenuItem( "Preferences...", "Ctrl+," ) )
				app.showPreferences = true;
			ImGui::EndMenu();
		}
		if( ImGui::BeginMenu( "Layer" ) )
		{
			if( ImGui::MenuItem( "New Layer" ) )
				app.composition.AddLayer();
			Composition& c = app.composition;
			std::string remove = "Delete " + ( c.selectedLayer >= 0 && c.selectedLayer < (int)c.layers.size() ? c.layers[ c.selectedLayer ].name : std::string( "Layer" ) );
			if( ImGui::MenuItem( remove.c_str(), nullptr, false, c.layers.size() > 1 && c.selectedLayer >= 0 && c.selectedLayer < (int)c.layers.size() ) )
				app.DeleteLayer( c.selectedLayer );
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
		if( ImGui::BeginMenu( "Clip" ) )
		{
			DrawClipMenu( app );
			ImGui::EndMenu();
		}
		if( ImGui::BeginMenu( "Output" ) )
		{
			DrawOutputModeItems( app );
			ImGui::EndMenu();
		}
		if( ImGui::BeginMenu( "Shortcuts" ) )
		{
			if( ImGui::MenuItem( "Map Keys", "Ctrl+K", app.learning && app.learnKeys ) )
				app.ToggleMapping( true );
			if( ImGui::MenuItem( "Map MIDI", "Ctrl+M", app.learning && !app.learnKeys ) )
				app.ToggleMapping( false );
			if( ImGui::MenuItem( "Shortcuts Panel (keys, MIDI, OSC)" ) )
			{
				app.panels.control = true;
				ImGui::SetWindowFocus( panel::Control );
			}
			ImGui::Separator();
			// Presets: the maps by name; the Shortcuts panel names and deletes them.
			if( ImGui::MenuItem( app.control.preset.empty() ? "Save Shortcuts..." : ( "Save Shortcuts (" + app.control.preset + ")" ).c_str(), nullptr, false,
			                     true ) )
			{
				if( app.control.preset.empty() )
				{
					app.panels.control = true;
					ImGui::SetWindowFocus( panel::Control );
					app.SetStatus( "Name the preset in the Shortcuts panel, then Save" );
				}
				else
					app.SaveShortcutPreset( app.control.preset );
			}
			if( ImGui::BeginMenu( "Load Shortcuts" ) )
			{
				std::vector< std::string > presets = app.ShortcutPresets();
				for( const std::string& name : presets )
					if( ImGui::MenuItem( name.c_str(), nullptr, name == app.control.preset ) )
						app.LoadShortcutPreset( name );
				if( presets.empty() )
					ImGui::TextDisabled( "No presets yet" );
				ImGui::EndMenu();
			}
			if( ImGui::MenuItem( "Import Shortcuts..." ) )
				ShowFileDialog( FileDialog::OpenShortcuts, DocumentsDirectory(), [ &app ]( const std::vector< std::string >& paths ) { app.ImportShortcuts( paths[ 0 ] ); } );
			if( ImGui::MenuItem( "Export Shortcuts..." ) )
				ShowFileDialog( FileDialog::SaveShortcuts, DocumentsDirectory(), [ &app ]( const std::vector< std::string >& paths ) { app.ExportShortcuts( paths[ 0 ] ); },
				                ( app.control.preset.empty() ? std::string( "STLT shortcuts" ) : app.control.preset ) + ".json" );
			ImGui::Separator();
			if( ImGui::MenuItem( "Clear All Keys", nullptr, false, !app.control.keys.empty() ) )
			{
				app.control.keys.clear();
				app.SaveControlSettings();
			}
			if( ImGui::MenuItem( "Clear All MIDI", nullptr, false, !app.control.midi.empty() ) )
			{
				app.control.midi.clear();
				app.SaveControlSettings();
			}
			ImGui::EndMenu();
		}
		if( ImGui::BeginMenu( "Move" ) )
		{
			if( ImGui::MenuItem( "Move Clips", nullptr, app.moveMode ) )
				app.SetMoveMode( !app.moveMode );
			if( ImGui::IsItemHovered() )
				ImGui::SetTooltip( "Pick clips in the deck (click, Ins, or drag a box), then drag them, or use the arrows, to move them together" );
			ImGui::Separator();
			if( ImGui::MenuItem( "Pick All Clips" ) )
			{
				if( !app.moveMode )
					app.SetMoveMode( true );
				app.PickAllClips();
			}
			if( ImGui::MenuItem( "Clear Picks", nullptr, false, !app.movePicked.empty() ) )
				app.movePicked.clear();
			ImGui::EndMenu();
		}
		if( ImGui::BeginMenu( "View" ) )
		{
			PanelVisibility& p = app.panels;
			ImGui::MenuItem( panel::Timeline, nullptr, &p.timeline );
			ImGui::MenuItem( panel::Layers, nullptr, &p.layers );
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
			ImGui::Separator();
			// The size of the whole interface: smaller on a small laptop, bigger on a large monitor.
			if( ImGui::MenuItem( "Zoom In", "Ctrl++" ) )
				app.ZoomInterface( 1 );
			if( ImGui::MenuItem( "Zoom Out", "Ctrl+-" ) )
				app.ZoomInterface( -1 );
			if( ImGui::MenuItem( "Actual Size", "Ctrl+0" ) )
				app.ZoomInterface( 0 );
			ImGui::TextDisabled( "Interface scale: %.0f %%", app.prefs.interfaceScale * 100 );
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
	// The interface's size: Ctrl + and Ctrl - (on the main keys or the keypad), Ctrl 0 back to 100 %.
	if( io.KeyCtrl && !io.KeyAlt && !io.WantTextInput && !app.learning )
	{
		if( ImGui::IsKeyPressed( ImGuiKey_Equal, true ) || ImGui::IsKeyPressed( ImGuiKey_KeypadAdd, true ) )
			app.ZoomInterface( 1 );
		if( ImGui::IsKeyPressed( ImGuiKey_Minus, true ) || ImGui::IsKeyPressed( ImGuiKey_KeypadSubtract, true ) )
			app.ZoomInterface( -1 );
		if( ImGui::IsKeyPressed( ImGuiKey_0, false ) || ImGui::IsKeyPressed( ImGuiKey_Keypad0, false ) )
			app.ZoomInterface( 0 );
	}
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
	if( io.KeyCtrl && !io.WantTextInput && ImGui::IsKeyPressed( ImGuiKey_Comma, false ) )
		app.showPreferences = !app.showPreferences;
	// The deck's clipboard (Session view: the timeline keeps its own keys).
	if( io.KeyCtrl && !io.KeyShift && !io.WantTextInput && app.view == App::View::Session && !app.learning )
	{
		if( ImGui::IsKeyPressed( ImGuiKey_C, false ) )
			app.CopyClip( false );
		else if( ImGui::IsKeyPressed( ImGuiKey_X, false ) )
			app.CopyClip( true );
		else if( ImGui::IsKeyPressed( ImGuiKey_V, false ) )
			app.PasteClip();
	}
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

// A thin vertical rule between groups of the toolbar.
void ToolbarDivider()
{
	ImGui::SameLine( 0, Ui( 14 ) );
	ImVec2 p = ImGui::GetCursorScreenPos();
	float h  = ImGui::GetFrameHeight();
	ImGui::GetWindowDrawList()->AddLine( ImVec2( p.x, p.y + h * 0.2f ), ImVec2( p.x, p.y + h * 0.8f ), theme::Line, 1.0f );
	ImGui::Dummy( ImVec2( 1, h ) );
	ImGui::SameLine( 0, Ui( 14 ) );
}

// A small dim caption in front of a control.
void ToolbarCaption( const char* text )
{
	ImGui::AlignTextToFramePadding();
	ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( theme::TextDim ), "%s", text );
	ImGui::SameLine( 0, Ui( 6 ) );
}

void DrawToolbar( App& app )
{
	Composition& comp = app.composition;
	ImGuiStyle& style = ImGui::GetStyle();
	ImGui::SetCursorPos( ImVec2( style.WindowPadding.x + Ui( 4 ), ImGui::GetCursorPosY() + Ui( 6 ) ) );
	ImGui::BeginGroup();

	if( app.view != App::View::Arrangement )
	{
		// Transport.
		if( IconButton( "##play", comp.paused ? Icon::Play : Icon::Pause, comp.paused ) )
			comp.paused = !comp.paused;
		if( ImGui::IsItemHovered() )
			ImGui::SetTooltip( comp.paused ? "Resume (Space)" : "Pause everything (Space)" );
		MapTarget( app, "/composition/pause" );
		ImGui::SameLine( 0, Ui( 4 ) );
		if( IconButton( "##stop", Icon::Stop ) )
		{
			for( int l = 0; l < (int)comp.layers.size(); ++l )
				app.ClearLayer( l );
			comp.activeColumn = -1;
		}
		if( ImGui::IsItemHovered() )
			ImGui::SetTooltip( "Clear every layer" );
		MapTarget( app, "/composition/stop" );
	}
	// Space alone pauses (with a modifier it is free for the user's shortcuts). The arrows: left and right launch the
	// previous and next clip on the selected layer, up and down select the layer above or below. Not while typing,
	// mapping or in the Advanced Output editor (its arrows move points), nor when the user mapped the key.
	const ImGuiIO& io = ImGui::GetIO();
	bool plainKeys    = !io.WantTextInput && !io.KeyCtrl && !io.KeyShift && !io.KeyAlt && !app.learning;
	// Arrangement: Space plays and stops the timeline, Left and Right go to the previous and next cue; Tab swaps the
	// views.
	bool arrangement = app.view == App::View::Arrangement;
	if( plainKeys && ImGui::IsKeyPressed( ImGuiKey_Tab, false ) )
		app.SetView( arrangement ? App::View::Session : App::View::Arrangement );
	if( plainKeys && ImGui::IsKeyPressed( ImGuiKey_Space, false ) )
	{
		if( arrangement )
			app.TimelinePlay( !app.composition.timeline.playing );
		else
			comp.paused = !comp.paused;
	}
	auto builtIn = [ & ]( ImGuiKey key ) {
		if( !plainKeys || app.panels.advancedOutput || !ImGui::IsKeyPressed( key, false ) )
			return false;
		for( const KeyMapping& m : app.control.keys )
			if( m.chord == KeyChord{ ImGui::GetKeyName( key ) } )
				return false;
		return true;
	};
	if( arrangement )
	{
		for( auto [ key, step ] : { std::pair{ ImGuiKey_RightArrow, 1 }, std::pair{ ImGuiKey_LeftArrow, -1 } } )
			if( plainKeys && !app.panels.advancedOutput && ImGui::IsKeyPressed( key, false ) )
				app.TimelineNextCue( step );
	}
	else if( app.moveMode )
	{
		// In Move mode the arrows move the picked clips by one cell.
		for( auto [ key, rows, columns ] : { std::tuple{ ImGuiKey_RightArrow, 0, 1 }, std::tuple{ ImGuiKey_LeftArrow, 0, -1 }, std::tuple{ ImGuiKey_UpArrow, 1, 0 },
		                                     std::tuple{ ImGuiKey_DownArrow, -1, 0 } } )
			if( plainKeys && !app.panels.advancedOutput && ImGui::IsKeyPressed( key, true ) )
				app.MovePicked( rows, columns );
	}
	else
	{
		if( builtIn( ImGuiKey_RightArrow ) )
			app.LaunchNextClip( comp.selectedLayer, 1 );
		if( builtIn( ImGuiKey_LeftArrow ) )
			app.LaunchNextClip( comp.selectedLayer, -1 );
		if( builtIn( ImGuiKey_UpArrow ) )
			comp.selectedLayer = std::min( comp.selectedLayer + 1, (int)comp.layers.size() - 1 );
		if( builtIn( ImGuiKey_DownArrow ) )
			comp.selectedLayer = std::max( comp.selectedLayer - 1, 0 );
	}

	// The deck's tempo, launch and speed: Session only (the timeline has its own in its panel).
	if( !arrangement )
	{
		// Tempo: the BPM in figures that do not jump about, halve, double, tap, resync, and the beats of the bar.
		ToolbarDivider();
		ToolbarCaption( "BPM" );
		float bpm = (float)comp.bpm;
		ImGui::PushFont( Fonts().mono );
		ImGui::SetNextItemWidth( Ui( 72 ) );
		if( ImGui::DragFloat( "##bpm", &bpm, 0.1f, 20.0f, 500.0f, "%.2f" ) )
			comp.bpm = bpm;
		ImGui::PopFont();
		if( ImGui::IsItemHovered() && !ImGui::IsItemActive() )
			ImGui::SetTooltip( "Drag, or double-click to type" );
		MapTarget( app, "/composition/tempocontroller/tempo" );
		ImGui::SameLine( 0, Ui( 4 ) );
		if( ImGui::Button( "/2" ) )
			comp.bpm = std::max( 20.0, comp.bpm / 2 );
		ImGui::SameLine( 0, Ui( 4 ) );
		if( ImGui::Button( "x2" ) )
			comp.bpm = std::min( 500.0, comp.bpm * 2 );
		ImGui::SameLine( 0, Ui( 4 ) );
		ImGui::PushStyleColor( ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4( theme::Accent ) );
		ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4( theme::AccentHover ) );
		ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImGui::ColorConvertU32ToFloat4( theme::AccentDim ) );
		ImGui::PushStyleColor( ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4( theme::OnAccent ) );
		ImGui::PushStyleColor( ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4( theme::Accent ) );
		ImGui::PushFont( Fonts().bold );
		if( ImGui::Button( " TAP " ) )
			app.TapTempo();
		ImGui::PopFont();
		ImGui::PopStyleColor( 5 );
		MapTarget( app, "/composition/tempocontroller/tempotap" );
		ImGui::SameLine( 0, Ui( 4 ) );
		if( ImGui::Button( "Resync" ) )
			app.ResyncBeat();
		if( ImGui::IsItemHovered() )
			ImGui::SetTooltip( "Start the beat over: the next beat begins now" );
		MapTarget( app, "/composition/tempocontroller/resync" );
		{
			// The four beats of the bar, the current one lit and fading towards the next.
			ImGui::SameLine( 0, Ui( 10 ) );
			ImVec2 p     = ImGui::GetCursorScreenPos();
			float h      = ImGui::GetFrameHeight();
			float radius = Ui( 3.5f );
			double beat  = std::max( comp.beat, 0.0 );
			int current  = (int)std::fmod( std::floor( beat ), 4.0 );
			float glow   = (float)( 1.0 - ( beat - std::floor( beat ) ) );
			for( int i = 0; i < 4; ++i )
			{
				ImVec2 c( p.x + radius + i * radius * 3.2f, p.y + h / 2 );
				ImU32 colour = theme::Raised;
				if( i == current )
				{
					ImVec4 from = ImGui::ColorConvertU32ToFloat4( theme::AccentDim ), to = ImGui::ColorConvertU32ToFloat4( theme::Accent );
					float t     = glow * glow;
					colour      = ImGui::GetColorU32( ImVec4( from.x + ( to.x - from.x ) * t, from.y + ( to.y - from.y ) * t, from.z + ( to.z - from.z ) * t, 1 ) );
				}
				ImGui::GetWindowDrawList()->AddCircleFilled( c, i == 0 ? radius * 1.15f : radius, colour );
			}
			ImGui::Dummy( ImVec2( radius * 2 + radius * 3.2f * 3, h ) );
		}
		// Ableton Link: on and off here, lit while on, with how many others share the tempo. A MIDI clock being
		// followed shows as CLOCK (chosen in Preferences > Sync).
		{
			TempoSync& sync = app.tempoSync;
			bool clock      = sync.Following() == TempoSync::Source::Clock;
			if( TempoSync::LinkBuiltIn() || clock )
				ImGui::SameLine( 0, Ui( 10 ) );
			if( TempoSync::LinkBuiltIn() )
			{
				bool on = sync.LinkEnabled();
				char label[ 32 ];
				int peers = sync.LinkPeers();
				if( on && peers > 0 )
					std::snprintf( label, sizeof( label ), "LINK %d###link", peers );
				else
					std::snprintf( label, sizeof( label ), "LINK###link" );
				if( on )
				{
					ImGui::PushStyleColor( ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4( theme::AccentDim ) );
					ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4( theme::AccentHover ) );
				}
				ImGui::PushFont( Fonts().bold );
				if( ImGui::Button( label ) )
				{
					app.prefs.linkEnabled = !on;
					app.ApplySync();
					app.SavePreferences();
				}
				ImGui::PopFont();
				if( on )
					ImGui::PopStyleColor( 2 );
				if( ImGui::IsItemHovered() )
					ImGui::SetTooltip( "Ableton Link: the tempo and beat shared with other apps on this network (Ableton Live, "
					                   "DJ software, other VJ apps).\n%s",
					                   on ? sync.Status().c_str() : "Off" );
			}
			if( clock )
			{
				if( TempoSync::LinkBuiltIn() )
					ImGui::SameLine( 0, Ui( 4 ) );
				ImGui::PushStyleColor( ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4( theme::AccentDim ) );
				ImGui::PushFont( Fonts().bold );
				ImGui::Button( "CLOCK" );
				ImGui::PopFont();
				ImGui::PopStyleColor();
				if( ImGui::IsItemHovered() )
					ImGui::SetTooltip( "%s\nThe tempo follows this MIDI clock (Preferences > Sync).", sync.Status().c_str() );
			}
		}

		// When launched clips start: at once, on the next beat or on the next bar.
		ToolbarDivider();
		ToolbarCaption( "Launch" );
		ImGui::SetNextItemWidth( Ui( 74 ) );
		EnumCombo( "##quantize", &comp.quantize, LaunchQuantizeName );
		if( ImGui::IsItemHovered() )
			ImGui::SetTooltip( "When a launched clip or column starts: now, on the next beat, or on the next bar (4 beats)" );
		// Where a launched clip plays: the layer of its row, or the selected layer whatever its row (layer based).
		ImGui::SameLine( 0, Ui( 10 ) );
		ToolbarCaption( "Clips to" );
		ImGui::SetNextItemWidth( Ui( 128 ) );
		EnumCombo( "##target", &comp.target, ClipTargetName );
		if( ImGui::IsItemHovered() )
			ImGui::SetTooltip( "Own Layer: a clip plays on the layer of its row.\nSelected Layer: select a layer (click its header), then any clip, from any row, plays there." );

	}

	if( arrangement )
	{
		// Arrangement: the timeline's transport and tempo are in its panel; the bar keeps its height.
		ImGui::Dummy( ImVec2( 1, ImGui::GetFrameHeight() ) );
	}

	// Right side: what the outputs show, and the frame rate.
	float fps = 0;
	if( !app.frameTimes.empty() )
	{
		float sum = 0;
		for( float t : app.frameTimes )
			sum += t;
		fps = sum > 0 ? 1000.0f * app.frameTimes.size() / sum : 0;
	}
	// The output mode, and whether anything is on show: a chip that opens the mode menu.
	bool shown = false;
	std::string output = "Output: " + app.OutputModeLabel();
	if( app.outputMode == OutputMode::Advanced )
	{
		int count = 0;
		for( size_t s = 0; s < app.LiveOutput().screens.size(); ++s )
			if( app.outputs.DeviceStatus( s ).rfind( "virtual", 0 ) != 0 && app.outputs.DeviceStatus( s ).find( "not connected" ) == std::string::npos )
				count++;
		shown = count > 0;
		output += " " + std::to_string( count ) + "/" + std::to_string( app.LiveOutput().screens.size() );
	}
	else
		shown = app.quickOutputs.AnyWindowOpen();
	char rate[ 32 ];
	std::snprintf( rate, sizeof( rate ), "%.0f fps", fps );
	ImGui::PushFont( Fonts().mono );
	float rateWidth = ImGui::CalcTextSize( rate ).x;
	ImGui::PopFont();
	float chipPad   = Ui( 10 );
	float chipWidth = ImGui::CalcTextSize( output.c_str() ).x + chipPad * 2 + Ui( 14 );
	float viewH     = ImGui::GetFrameHeight();
	float viewW     = viewH * 1.4f;
	ImGui::SameLine( ImGui::GetWindowWidth() - ( chipWidth + Ui( 12 ) + rateWidth + Ui( 28 ) + 1 + viewW * 2 + Ui( 2 ) ) - style.WindowPadding.x - Ui( 4 ) );
	{
		// A chip: a dot (lit when something is on show) and the mode.
		ImVec2 p      = ImGui::GetCursorScreenPos();
		float h       = ImGui::GetFrameHeight();
		bool clicked  = ImGui::InvisibleButton( "##outputChip", ImVec2( chipWidth, h ) );
		bool hovered  = ImGui::IsItemHovered();
		ImDrawList* d = ImGui::GetWindowDrawList();
		d->AddRectFilled( p, ImVec2( p.x + chipWidth, p.y + h ), hovered ? theme::RaisedHover : theme::Well );
		d->AddRect( p, ImVec2( p.x + chipWidth, p.y + h ), theme::Line );
		d->AddCircleFilled( ImVec2( p.x + chipPad + Ui( 3 ), p.y + h / 2 ), Ui( 3.5f ), shown ? theme::Accent : theme::TextDim );
		d->AddText( ImVec2( p.x + chipPad + Ui( 14 ), p.y + ( h - ImGui::GetTextLineHeight() ) / 2 ), shown ? theme::Text : theme::TextDim, output.c_str() );
		if( hovered )
			ImGui::SetTooltip( "Where the composition goes: off, a window, fullscreen on a display, or the Advanced Output setup" );
		if( clicked )
			ImGui::OpenPopup( "##outputMode" );
		if( ImGui::BeginPopup( "##outputMode" ) )
		{
			DrawOutputModeItems( app );
			ImGui::EndPopup();
		}
	}
	ImGui::SameLine( 0, Ui( 12 ) );
	ImGui::AlignTextToFramePadding();
	ImGui::PushFont( Fonts().mono );
	ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( fps >= 50 || fps == 0 ? theme::TextDim : theme::Warning ), "%s", rate );
	ImGui::PopFont();
	ToolbarDivider();
	{
		// The two views, last on the right: Session (the grid of clips) and Arrangement (the timeline). Tab switches.
		for( int v = 0; v < 2; ++v )
		{
			if( v > 0 )
				ImGui::SameLine( 0, Ui( 2 ) );
			ImVec2 p      = ImGui::GetCursorScreenPos();
			bool on       = ( v == 1 ) == ( app.view == App::View::Arrangement );
			bool clicked  = ImGui::InvisibleButton( v == 0 ? "##sessionView" : "##arrangementView", ImVec2( viewW, viewH ) );
			bool hovered  = ImGui::IsItemHovered();
			ImDrawList* d = ImGui::GetWindowDrawList();
			ImVec2 q( p.x + viewW, p.y + viewH );
			if( on )
				d->AddRectFilled( p, q, theme::Accent );
			else
			{
				if( hovered )
					d->AddRectFilled( p, q, theme::RaisedHover );
				d->AddRect( p, q, theme::Edge );
			}
			DrawIcon( d, v == 0 ? Icon::Grid : Icon::Lines, p, q, on ? theme::OnAccent : hovered ? theme::Text : theme::TextSoft );
			if( hovered )
				ImGui::SetTooltip( v == 0 ? "Grid: the deck of clips to launch (Tab)" : "Timeline: the show laid out in time (Tab)" );
			if( clicked )
				app.SetView( v == 0 ? App::View::Session : App::View::Arrangement );
		}
	}
	ImGui::EndGroup();
	ImGui::SetCursorPosY( ImGui::GetCursorPosY() + Ui( 4 ) );
}

// ---- The deck: the Layers panel (the composition's row, a header per layer, the crossfader) beside the Clips panel
// (the column headers, the cells, the deck tabs). Each header is made of blocks: the layer (name, clear, black,
// solo, blend), its faders (video, audio, master, crossfader group), the clip on show (picture, position) and that
// clip's controls (speed, play mode, timing, previous/next, direction).

const Icon kCloseIcon = Icon::Close;

// How a drawn button looks: an outline (most), a quiet filled box (transport), or lit (a toggle that is on but
// not the accent's business, such as a crossfader group).
enum class BoxStyle
{
	Outline,
	Quiet,
};

// A flat button of any size with a label or an icon: the accent fill when `on`.
static bool BoxButton( const char* id, ImVec2 pos, ImVec2 size, const char* label, bool on, ImU32 onColour, const char* tip = nullptr,
                       const Icon* icon = nullptr, BoxStyle style = BoxStyle::Outline, ImFont* font = nullptr )
{
	ImGui::SetCursorScreenPos( pos );
	bool clicked     = ImGui::InvisibleButton( id, size );
	bool hovered     = ImGui::IsItemHovered();
	bool disabled    = ImGui::GetItemFlags() & ImGuiItemFlags_Disabled;
	ImDrawList* draw = ImGui::GetWindowDrawList();
	ImVec2 max( pos.x + size.x, pos.y + size.y );
	if( on )
		draw->AddRectFilled( pos, max, onColour );
	else if( hovered )
		draw->AddRectFilled( pos, max, theme::RaisedHover );
	else if( style == BoxStyle::Quiet )
		draw->AddRectFilled( pos, max, theme::Raised );
	if( !on && style == BoxStyle::Outline )
		draw->AddRect( pos, max, theme::Edge );
	// On the accent: dark ink; on another colour (lit, not accented): bright ink.
	ImU32 fg = on ? ( onColour == theme::Accent ? theme::OnAccent : theme::Text ) : hovered ? theme::Text : theme::TextSoft;
	if( disabled )
		fg = ( fg & 0x00FFFFFF ) | 0x70000000;
	if( icon )
	{
		float s = std::min( size.x, size.y ) * 0.8f;
		ImVec2 c( ( pos.x + max.x ) / 2, ( pos.y + max.y ) / 2 );
		DrawIcon( draw, *icon, ImVec2( c.x - s / 2, c.y - s / 2 ), ImVec2( c.x + s / 2, c.y + s / 2 ), fg );
	}
	else
	{
		ImGui::PushFont( font ? font : Fonts().bold );
		ImVec2 t = ImGui::CalcTextSize( label );
		draw->PushClipRect( pos, max, true );
		draw->AddText( ImVec2( pos.x + std::max( Ui( 3 ), ( size.x - t.x ) / 2 ), pos.y + ( size.y - t.y ) / 2 ), fg, label );
		draw->PopClipRect();
		ImGui::PopFont();
	}
	if( tip && ImGui::IsItemHovered( ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled ) )
		ImGui::SetTooltip( "%s", tip );
	return clicked;
}

static bool IconBox( const char* id, ImVec2 pos, ImVec2 size, Icon icon, bool on, const char* tip = nullptr, BoxStyle style = BoxStyle::Outline )
{
	// Transport (the quiet boxes) lights up grey when on: the accent is for what is on air.
	return BoxButton( id, pos, size, "", on, style == BoxStyle::Quiet ? theme::Edge : theme::Accent, tip, &icon, style );
}

// A drop-down drawn as a box: an icon or a short label, and a caret. Returns true when clicked (open its popup).
static bool DropBox( const char* id, ImVec2 pos, ImVec2 size, const char* label, const Icon* icon, const char* tip )
{
	ImGui::SetCursorScreenPos( pos );
	bool clicked     = ImGui::InvisibleButton( id, size );
	bool hovered     = ImGui::IsItemHovered();
	bool disabled    = ImGui::GetItemFlags() & ImGuiItemFlags_Disabled;
	ImDrawList* draw = ImGui::GetWindowDrawList();
	ImVec2 max( pos.x + size.x, pos.y + size.y );
	if( hovered )
		draw->AddRectFilled( pos, max, theme::RaisedHover );
	draw->AddRect( pos, max, theme::Edge );
	ImU32 fg = hovered ? theme::Text : theme::TextSoft;
	if( disabled )
		fg = ( fg & 0x00FFFFFF ) | 0x70000000;
	float caret = Ui( 10 );
	DrawIcon( draw, Icon::CaretDown, ImVec2( max.x - caret - Ui( 3 ), pos.y + ( size.y - caret ) / 2 ), ImVec2( max.x - Ui( 3 ), pos.y + ( size.y + caret ) / 2 ), fg );
	if( icon )
	{
		float s = std::min( size.y, Ui( 18 ) );
		DrawIcon( draw, *icon, ImVec2( pos.x + Ui( 4 ), pos.y + ( size.y - s ) / 2 ), ImVec2( pos.x + Ui( 4 ) + s, pos.y + ( size.y + s ) / 2 ), fg );
	}
	else
	{
		ImGui::PushFont( Fonts().bold );
		draw->PushClipRect( pos, ImVec2( max.x - caret - Ui( 3 ), max.y ), true );
		draw->AddText( ImVec2( pos.x + Ui( 6 ), pos.y + ( size.y - ImGui::GetTextLineHeight() ) / 2 ), fg, label );
		draw->PopClipRect();
		ImGui::PopFont();
	}
	if( tip && ImGui::IsItemHovered( ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_AllowWhenDisabled ) && !ImGui::IsPopupOpen( "", ImGuiPopupFlags_AnyPopupId ) )
		ImGui::SetTooltip( "%s", tip );
	return clicked;
}

// A menu entry with a drawn icon before its name.
static bool IconSelectable( const char* name, Icon icon, bool chosen )
{
	ImVec2 p = ImGui::GetCursorScreenPos();
	float s  = ImGui::GetTextLineHeight();
	std::string label = std::string( "      " ) + name;
	bool clicked      = ImGui::Selectable( label.c_str(), chosen );
	DrawIcon( ImGui::GetWindowDrawList(), icon, ImVec2( p.x, p.y ), ImVec2( p.x + s, p.y + s ), chosen ? theme::Accent : theme::TextSoft );
	return clicked;
}

// A vertical fader: its value above, its letter below, filled from the bottom up to the value. Drag it;
// right-click or double-click: 100 %.
static bool VerticalFader( const char* id, ImVec2 pos, ImVec2 size, float* value, const char* letter, const char* shown, const char* name, bool learning, bool hatched )
{
	ImFont* small      = Fonts().small;
	ImFont* smallMono  = Fonts().smallMono;
	const float textH  = small->FontSize;
	const float trackT = pos.y + textH + Ui( 2 ), trackB = pos.y + size.y - textH - Ui( 2 );
	ImGui::SetCursorScreenPos( pos );
	ImGui::InvisibleButton( id, size );
	bool hovered = ImGui::IsItemHovered(), changed = false;
	float trackH = std::max( 1.0f, trackB - trackT );
	if( ImGui::IsItemActive() )
	{
		float v = std::clamp( ( trackB - ImGui::GetIO().MousePos.y ) / trackH, 0.0f, 1.0f );
		changed = v != *value;
		*value  = v;
	}
	changed |= ResetValue( value, 1.0f );
	ImDrawList* draw = ImGui::GetWindowDrawList();
	ImVec2 t         = smallMono->CalcTextSizeA( smallMono->FontSize, FLT_MAX, 0, shown );
	draw->AddText( smallMono, smallMono->FontSize, ImVec2( pos.x + ( size.x - t.x ) / 2, pos.y ), hatched ? theme::TextDim : theme::Text, shown );
	ImVec2 a( pos.x, trackT ), b( pos.x + size.x, trackB );
	draw->AddRectFilled( a, b, hovered ? IM_COL32( 22, 20, 18, 255 ) : theme::Well );
	float top = trackB - trackH * std::clamp( *value, 0.0f, 1.0f );
	if( top < trackB - 0.5f )
	{
		if( hatched )
		{
			// The layer is off: its faders striped, so they do not read as live.
			draw->PushClipRect( ImVec2( a.x, top ), b, true );
			for( float x = a.x - trackH; x < b.x; x += Ui( 6 ) )
				draw->AddLine( ImVec2( x, b.y ), ImVec2( x + trackH, b.y - trackH ), theme::Line, Ui( 2 ) );
			draw->PopClipRect();
		}
		else
			draw->AddRectFilled( ImVec2( a.x, top ), b, theme::Fill );
	}
	draw->AddRectFilled( ImVec2( a.x, top - Ui( 1 ) ), ImVec2( b.x, top + Ui( 1 ) ), hatched ? theme::TextFaint : theme::Text );
	draw->AddRect( a, b, theme::Line );
	t = small->CalcTextSizeA( textH, FLT_MAX, 0, letter );
	draw->AddText( small, textH, ImVec2( pos.x + ( size.x - t.x ) / 2, trackB + Ui( 2 ) ), theme::TextDim, letter );
	if( hovered && !learning )
		ImGui::SetTooltip( "%s %.0f %%\nRight-click: 100 %%", name, *value * 100 );
	return changed;
}

// A horizontal fader with its letter before it and (optionally) its value after it: drag along it; right-click or
// double-click: `reset`.
static bool HorizontalFader( const char* id, ImVec2 pos, ImVec2 size, float* value, float min, float max, float reset, const char* letter, const char* shown,
                             float shownW )
{
	ImFont* small   = Fonts().small;
	float letterW   = letter ? small->CalcTextSizeA( small->FontSize, FLT_MAX, 0, letter ).x + Ui( 5 ) : 0;
	ImVec2 a( pos.x + letterW, pos.y + Ui( 3 ) ), b( pos.x + size.x - shownW, pos.y + size.y - Ui( 3 ) );
	ImGui::SetCursorScreenPos( pos );
	ImGui::InvisibleButton( id, size );
	bool hovered  = ImGui::IsItemHovered();
	bool disabled = ImGui::GetItemFlags() & ImGuiItemFlags_Disabled;
	bool changed  = false;
	float width   = std::max( 1.0f, b.x - a.x );
	if( ImGui::IsItemActive() && max > min )
	{
		float v = min + std::clamp( ( ImGui::GetIO().MousePos.x - a.x ) / width, 0.0f, 1.0f ) * ( max - min );
		changed = v != *value;
		*value  = v;
	}
	if( !disabled )
		changed |= ResetValue( value, reset );
	ImDrawList* draw = ImGui::GetWindowDrawList();
	if( letter )
		draw->AddText( small, small->FontSize, ImVec2( pos.x, pos.y + ( size.y - small->FontSize ) / 2 ), theme::TextDim, letter );
	draw->AddRectFilled( a, b, hovered ? IM_COL32( 22, 20, 18, 255 ) : theme::Well );
	float x = a.x + width * ( max > min ? std::clamp( ( *value - min ) / ( max - min ), 0.0f, 1.0f ) : 0 );
	if( x > a.x + 0.5f )
		draw->AddRectFilled( a, ImVec2( x, b.y ), disabled ? theme::Raised : theme::Fill );
	draw->AddRect( a, b, theme::Line );
	draw->AddRectFilled( ImVec2( x - Ui( 1.5f ), a.y - Ui( 2 ) ), ImVec2( x + Ui( 1.5f ), b.y + Ui( 2 ) ), disabled ? theme::TextFaint : theme::Text );
	if( shown )
	{
		ImGui::PushFont( Fonts().mono );
		ImVec2 t = ImGui::CalcTextSize( shown );
		draw->AddText( ImVec2( pos.x + size.x - t.x, pos.y + ( size.y - t.y ) / 2 ), disabled ? theme::TextDim : theme::Text, shown );
		ImGui::PopFont();
	}
	return changed;
}

// Switches a clip between its own speed and BPM sync (the nearest power of two of beats at this tempo).
static void SetBpmSync( Clip& clip, bool on, double bpm )
{
	if( !on )
	{
		clip.syncBeats = 0;
		return;
	}
	if( clip.syncBeats > 0 )
		return;
	double beats   = ( clip.RangeEnd() - clip.RangeStart() ) * bpm / 60.0;
	clip.syncBeats = (float)std::clamp( std::exp2( std::round( std::log2( std::max( beats, 0.25 ) ) ) ), 0.25, 256.0 );
}

// Where the clip is: a bar to see and drag (scratch) the playhead.
static void PositionBar( const char* id, ImVec2 pos, ImVec2 size, Clip* clip, bool dim )
{
	ImGui::SetCursorScreenPos( pos );
	ImGui::InvisibleButton( id, size );
	ImDrawList* draw = ImGui::GetWindowDrawList();
	ImVec2 max( pos.x + size.x, pos.y + size.y );
	draw->AddRectFilled( pos, max, ImGui::IsItemHovered() ? IM_COL32( 22, 20, 18, 255 ) : theme::Well );
	draw->AddRect( pos, max, theme::Line );
	double duration = clip && clip->Loaded() ? clip->Duration() : 0;
	if( duration <= 0 )
		return;
	if( ImGui::IsItemActive() )
		clip->JumpTo( std::clamp( ( ImGui::GetIO().MousePos.x - pos.x ) / size.x, 0.0f, 1.0f ) * duration );
	float x0 = pos.x + size.x * (float)( clip->RangeStart() / duration ), x1 = pos.x + size.x * (float)( clip->RangeEnd() / duration );
	float x  = pos.x + size.x * (float)( std::clamp( clip->time, 0.0, duration ) / duration );
	if( x > x0 )
		draw->AddRectFilled( ImVec2( x0, pos.y + 1 ), ImVec2( x, max.y - 1 ), dim ? theme::Raised : theme::Fill );
	if( x1 < max.x - 1 )
		draw->AddRectFilled( ImVec2( x1, pos.y + 1 ), ImVec2( max.x - 1, max.y - 1 ), IM_COL32( 0, 0, 0, 120 ) );
	draw->AddRectFilled( ImVec2( x - Ui( 1.5f ), pos.y ), ImVec2( x + Ui( 1.5f ), max.y ), dim ? theme::TextFaint : theme::Text );
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "%s / %s\nDrag to scratch", FormatTime( clip->time ).c_str(), FormatTime( duration ).c_str() );
}

// The crossfader: layers in group A on the left, group B on the right; in the middle both show in full.
static void Crossfader( App& app, ImVec2 pos, ImVec2 size )
{
	Composition& comp = app.composition;
	ImFont* small     = Fonts().small;
	float letter      = Ui( 14 );
	ImDrawList* draw  = ImGui::GetWindowDrawList();
	ImVec2 bar( pos.x + letter, pos.y + ( size.y - Ui( 16 ) ) / 2 );
	float barW = size.x - 2 * letter, barH = Ui( 16 );
	ImGui::SetCursorScreenPos( pos );
	if( ImGui::InvisibleButton( "##xfA", ImVec2( letter, size.y ) ) )
		comp.crossfader = 0;
	draw->AddText( small, small->FontSize, ImVec2( pos.x + Ui( 2 ), pos.y + ( size.y - small->FontSize ) / 2 ), comp.crossfader <= 0 ? theme::Accent : theme::TextDim, "A" );
	ImGui::SetCursorScreenPos( bar );
	ImGui::InvisibleButton( "##crossfader", ImVec2( barW, barH ) );
	if( ImGui::IsItemActive() )
		comp.crossfader = std::clamp( ( ImGui::GetIO().MousePos.x - bar.x ) / barW, 0.0f, 1.0f );
	ResetValue( &comp.crossfader, 0.5f );
	bool hovered = ImGui::IsItemHovered();
	draw->AddRectFilled( bar, ImVec2( bar.x + barW, bar.y + barH ), hovered ? IM_COL32( 22, 20, 18, 255 ) : theme::Well );
	draw->AddRect( bar, ImVec2( bar.x + barW, bar.y + barH ), theme::Line );
	draw->AddLine( ImVec2( bar.x + barW / 2, bar.y + Ui( 3 ) ), ImVec2( bar.x + barW / 2, bar.y + barH - Ui( 3 ) ), theme::Edge );
	float x = bar.x + barW * comp.crossfader, handle = Ui( 5 );
	draw->AddRectFilled( ImVec2( x - handle, bar.y - Ui( 3 ) ), ImVec2( x + handle, bar.y + barH + Ui( 3 ) ), theme::Text );
	if( hovered && !app.learning )
		ImGui::SetTooltip( "Crossfader: layers in group A (A under their faders) on the left, group B on the right\nRight-click: middle" );
	MapTarget( app, "/composition/crossfader/phase" );
	ImGui::SetCursorScreenPos( ImVec2( bar.x + barW, pos.y ) );
	if( ImGui::InvisibleButton( "##xfB", ImVec2( letter, size.y ) ) )
		comp.crossfader = 1;
	draw->AddText( small, small->FontSize, ImVec2( bar.x + barW + Ui( 4 ), pos.y + ( size.y - small->FontSize ) / 2 ), comp.crossfader >= 1 ? theme::Accent : theme::TextDim, "B" );
}

// A dashed rectangle (ImGui draws only solid ones): the frame of a clip waiting for its beat, an empty cell.
static void DashedRect( ImDrawList* draw, ImVec2 a, ImVec2 b, ImU32 colour, float thickness, float dash )
{
	auto line = [ & ]( ImVec2 p, ImVec2 q ) {
		float length = std::hypot( q.x - p.x, q.y - p.y );
		if( length <= 0 )
			return;
		ImVec2 d( ( q.x - p.x ) / length, ( q.y - p.y ) / length );
		for( float s = 0; s < length; s += dash * 2 )
		{
			float e = std::min( s + dash, length );
			draw->AddLine( ImVec2( p.x + d.x * s, p.y + d.y * s ), ImVec2( p.x + d.x * e, p.y + d.y * e ), colour, thickness );
		}
	};
	float h = thickness / 2;
	line( ImVec2( a.x, a.y + h ), ImVec2( b.x, a.y + h ) );
	line( ImVec2( b.x - h, a.y ), ImVec2( b.x - h, b.y ) );
	line( ImVec2( b.x, b.y - h ), ImVec2( a.x, b.y - h ) );
	line( ImVec2( a.x + h, b.y ), ImVec2( a.x + h, a.y ) );
}

// A small dark badge with a word in the corner of a picture (FX, T, P...).
static float Badge( ImDrawList* draw, ImVec2 at, const char* text, ImU32 bg, ImU32 fg, bool fromRight )
{
	ImFont* small = Fonts().small;
	ImVec2 t      = small->CalcTextSizeA( small->FontSize, FLT_MAX, 0, text );
	float w = t.x + Ui( 6 ), h = t.y + Ui( 2 );
	ImVec2 a( fromRight ? at.x - w : at.x, at.y );
	draw->AddRectFilled( a, ImVec2( a.x + w, a.y + h ), bg );
	draw->AddText( small, small->FontSize, ImVec2( a.x + Ui( 3 ), a.y + Ui( 1 ) ), fg, text );
	return w;
}

// Short names for the blend modes, for the layer headers (the menu has the full ones).
static const char* BlendModeShort( BlendMode mode )
{
	static const char* names[] = { "ALP", "ADD", "SCR", "MUL", "LGT", "DRK", "DIF", "SUB", "LUM", "LMI" };
	return names[ std::clamp( (int)mode, 0, (int)BlendMode::Count - 1 ) ];
}

static Icon PlayModeIcon( PlayMode mode )
{
	switch( mode )
	{
	case PlayMode::Bounce: return Icon::Bounce;
	case PlayMode::Once: return Icon::Hold;
	case PlayMode::OnceClear: return Icon::OnceClear;
	default: return Icon::Loop;
	}
}

// The deck's geometry, shared by both panels so that a layer's header and its cells line up.
struct DeckGeometry
{
	float top, row, hair, cellW, footH, pad;
	float layerW, fadersW, clipW, controlsW, headerW;
	DeckGeometry()
	{
		top       = Ui( 38 );
		row       = Ui( 100 );
		hair      = std::max( 1.0f, std::floor( Ui( 1 ) ) );
		cellW     = Ui( 134 );
		footH     = Ui( 36 );
		pad       = Ui( 7 );
		layerW    = Ui( 120 );
		fadersW   = Ui( 110 );
		clipW     = Ui( 134 );
		controlsW = Ui( 178 );
		headerW   = layerW + fadersW + clipW + controlsW;
	}
};

float LayersPanelWidth()
{
	return DeckGeometry().headerW + ImGui::GetStyle().WindowPadding.x * 2 + Ui( 2 );
}

float DeckPanelsHeight( int layers )
{
	DeckGeometry g;
	const ImGuiStyle& style = ImGui::GetStyle();
	float tab               = ImGui::GetFontSize() + style.FramePadding.y * 2;
	return tab + style.WindowPadding.y * 2 + g.top + g.hair + layers * ( g.row + g.hair ) + g.footH + Ui( 4 );
}

// The deck in two panels: Layers (the composition's row, each layer's header, the crossfader) and Clips (the column
// headers, cells and deck tabs). Same geometry in both, so a layer's header and its clips line up when the panels
// sit side by side.
static void DrawDeck( App& app, bool layersPart )
{
	Composition& comp = app.composition;
	if( !ImGui::Begin( layersPart ? panel::Layers : panel::Clips, layersPart ? &app.panels.layers : &app.panels.clips ) )
	{
		ImGui::End();
		return;
	}
	const DeckGeometry g;
	const float cellW  = g.cellW;
	const float pad    = Ui( 6 );                          // inside a cell
	const float picW   = cellW - 2 * pad;
	const float picH   = std::floor( picW * 9 / 16 );
	const float stripH = g.row - 2 * pad - picH;           // the name under the picture
	ImDrawList* draw   = ImGui::GetWindowDrawList();
	bool dropPending   = !app.droppedPaths.empty();
	ImVec2 dropPoint( app.dropX, app.dropY );
	// Loads may add columns and move clips in memory, so they wait until the grid is drawn.
	struct PendingLoad
	{
		int layer = -1, column = -1;
		std::vector< std::string > paths;
		std::string generatorId;
	} pendingLoad;

	// Decks, under the cells (level with the Layers panel's crossfader): a tab each (a dot when a clip of it plays
	// while another deck is shown), + adds one. Double-click or right-click a tab to rename or remove it.
	auto deckTabs = [ & ]( ImVec2 pos, float h )
	{
		static std::string renameText;
		static int renaming = -1;
		float x = pos.x;
		for( int d = 0; d < comp.DeckCount(); ++d )
		{
			ImGui::PushID( d );
			bool shown   = d == comp.deck;
			bool playing = false;
			for( const Layer& layer : comp.layers )
				playing |= layer.active >= 0 && layer.activeDeck == d;
			ImGui::PushFont( shown ? Fonts().bold : Fonts().body );
			float w = ImGui::CalcTextSize( comp.decks[ d ].name.c_str() ).x + Ui( 22 );
			ImGui::PopFont();
			if( BoxButton( "##deck", ImVec2( x, pos.y ), ImVec2( w, h ), comp.decks[ d ].name.c_str(), shown, theme::Accent, nullptr, nullptr, BoxStyle::Outline,
			               shown ? Fonts().bold : Fonts().body ) )
				app.ShowDeck( d );
			if( playing && !shown )
				draw->AddCircleFilled( ImVec2( x + Ui( 6 ), pos.y + h / 2 ), Ui( 2.5f ), theme::Accent );
			bool open = ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left );
			if( ImGui::IsItemHovered() && !app.learning )
				ImGui::SetTooltip( "%s%s\nDouble-click or right-click: rename or remove", comp.decks[ d ].name.c_str(),
				                   playing && !shown ? " (a clip of it is playing)" : "" );
			if( ImGui::IsItemHovered() && ImGui::IsMouseClicked( ImGuiMouseButton_Right ) )
				open = true;
			if( open )
			{
				renaming   = d;
				renameText = comp.decks[ d ].name;
				ImGui::OpenPopup( "##deckMenu" );
			}
			bool removed = false;
			if( ImGui::BeginPopup( "##deckMenu" ) )
			{
				ImGui::TextDisabled( "Deck name" );
				ImGui::SetNextItemWidth( Ui( 180 ) );
				if( ImGui::IsWindowAppearing() )
					ImGui::SetKeyboardFocusHere();
				bool done = ImGui::InputText( "##name", &renameText, ImGuiInputTextFlags_EnterReturnsTrue );
				if( ( done || ImGui::IsItemDeactivatedAfterEdit() ) && renaming == d && !renameText.empty() )
					comp.decks[ d ].name = renameText;
				if( done )
					ImGui::CloseCurrentPopup();
				ImGui::Separator();
				if( ImGui::MenuItem( "Remove Deck", nullptr, false, comp.DeckCount() > 1 ) )
				{
					app.RemoveDeck( d );
					removed = true;
				}
				ImGui::EndPopup();
			}
			if( !removed )
				MapTarget( app, "/composition/decks/" + std::to_string( d + 1 ) + "/select" );
			ImGui::PopID();
			if( removed )
				break;
			x += w + Ui( 4 );
		}
		static const Icon plus = Icon::Plus;
		if( BoxButton( "##addDeck", ImVec2( x, pos.y ), ImVec2( h * 1.2f, h ), "", false, theme::Accent,
		               app.learning ? nullptr : "New deck: another page of clips over the same layers.\nWhat plays keeps playing when you change deck.", &plus ) )
			app.AddDeck();
		return x + h * 1.2f;
	};

	// Move mode: a banner with what to do, and the box being dragged to pick clips.
	struct PickBox
	{
		bool active = false, additive = false;
		ImVec2 start;
	};
	static PickBox box;
	static CellRef moveTarget{ -1, -1 }; // the cell under the dragged clips, last frame
	static CellRef pianoHeld{ -1, -1 };  // a Piano clip held down with the mouse
	const ImGuiIO& io     = ImGui::GetIO();
	bool insertPressed    = app.moveMode && !io.WantTextInput && ImGui::IsKeyPressed( ImGuiKey_Insert, false );
	bool insertHandled    = false;
	if( !app.moveMode )
		box.active = false;
	bool boxRelease       = box.active && !ImGui::IsMouseDown( ImGuiMouseButton_Left );
	ImVec2 boxMin( std::min( box.start.x, io.MousePos.x ), std::min( box.start.y, io.MousePos.y ) );
	ImVec2 boxMax( std::max( box.start.x, io.MousePos.x ), std::max( box.start.y, io.MousePos.y ) );
	const ImGuiPayload* dragged = ImGui::GetDragDropPayload();
	bool movingPicked           = app.moveMode && dragged && dragged->IsDataType( kMovePayload );
	CellRef moveFrom            = movingPicked ? *(const CellRef*)dragged->Data : CellRef{ -1, -1 };
	if( !movingPicked )
		moveTarget = { -1, -1 };
	CellRef nextMoveTarget{ -1, -1 };
	if( app.moveMode && layersPart )
		ImGui::Dummy( ImVec2( 1, ImGui::GetFrameHeightWithSpacing() + Ui( 6 ) ) ); // as high as the banner beside it
	else if( app.moveMode )
	{
		ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4( 0.45f, 0.22f, 0.55f, 0.55f ) );
		ImGui::BeginChild( "##moveBanner", ImVec2( 0, ImGui::GetFrameHeightWithSpacing() + Ui( 6 ) ), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar );
		ImGui::SetCursorPos( ImVec2( Ui( 8 ), Ui( 3 ) ) );
		if( ImGui::Button( "Done" ) )
			app.SetMoveMode( false );
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		ImGui::Text( "Move: %d picked. Click clips or press Ins to pick them, drag a box from an empty cell to pick many, then drag a picked clip "
		             "(or press the arrows) to move them all.",
		             (int)app.movePicked.size() );
		ImGui::EndChild();
		ImGui::PopStyleColor();
		if( !io.WantTextInput && !app.learning && ImGui::IsKeyPressed( ImGuiKey_Escape, false ) && !ImGui::IsPopupOpen( "", ImGuiPopupFlags_AnyPopup ) )
			app.SetMoveMode( false );
	}

	ImGui::BeginChild( layersPart ? "##layerHeaders" : "##grid", ImVec2( 0, 0 ), ImGuiChildFlags_None,
	                   layersPart ? ImGuiWindowFlags_NoScrollbar : ImGuiWindowFlags_HorizontalScrollbar );
	{
		// The Layers and Clips panels scroll up and down together, so each layer's header stays beside its clips.
		static float shared = 0, own[ 2 ] = { 0, 0 };
		float& mine           = own[ layersPart ? 1 : 0 ];
		float now             = ImGui::GetScrollY();
		if( now != mine )
			shared = now;
		else if( shared != now )
			ImGui::SetScrollY( shared );
		mine = now != mine ? now : shared;
	}
	draw         = ImGui::GetWindowDrawList();
	ImVec2 origin = ImGui::GetCursorScreenPos();
	// The headers stay at the left of the view while the deck scrolls sideways: column headers and cells are cut
	// off where the headers end.
	const float stickyX  = origin.x + ImGui::GetScrollX();
	const ImVec2 viewMin = ImGui::GetWindowPos();
	const ImVec2 viewMax( viewMin.x + ImGui::GetWindowWidth(), viewMin.y + ImGui::GetWindowHeight() );
	const float cellsLeft = layersPart ? viewMax.x : stickyX;
	const float x0 = stickyX, x1 = x0 + g.layerW, x2 = x1 + g.fadersW, x3 = x2 + g.clipW, xEnd = x0 + g.headerW;
	const bool learning = app.learning;
	auto tip            = [ & ]( const char* text ) { return learning ? nullptr : text; };
	const int layerCount = (int)comp.layers.size();
	const float rowsTop  = origin.y + g.top + g.hair;
	const float bottom   = rowsTop + layerCount * ( g.row + g.hair );
	auto hline           = [ & ]( float y, float from, float to ) { draw->AddRectFilled( ImVec2( from, y ), ImVec2( to, y + g.hair ), theme::Line ); };
	auto vline           = [ & ]( float x, float from, float to ) { draw->AddRectFilled( ImVec2( x, from ), ImVec2( x + g.hair, to ), theme::Line ); };

	// The composition, on one line: its name, clear, black, master, speed, the transport of every clip and the
	// previous/next column.
	if( layersPart )
	{
		const float y = origin.y, h = Ui( 26 ), cy = y + ( g.top - h ) / 2;
		float x = x0 + Ui( 6 );
		ImGui::PushFont( Fonts().bold );
		ImVec2 t = ImGui::CalcTextSize( comp.name.c_str() );
		float nameW = std::min( t.x, Ui( 110 ) );
		ImGui::SetCursorScreenPos( ImVec2( x, cy ) );
		if( ImGui::InvisibleButton( "##compName", ImVec2( nameW, h ) ) )
			ImGui::SetWindowFocus( panel::Composition );
		if( ImGui::IsItemHovered() && !learning )
			ImGui::SetTooltip( "%s: its settings and effects in the Composition panel", comp.name.c_str() );
		draw->PushClipRect( ImVec2( x, y ), ImVec2( x + nameW, y + g.top ), true );
		draw->AddText( ImVec2( x, y + ( g.top - t.y ) / 2 ), theme::Text, comp.name.c_str() );
		draw->PopClipRect();
		ImGui::PopFont();
		x += nameW + Ui( 2 );
		if( BoxButton( "##compClear", ImVec2( x, cy ), ImVec2( Ui( 20 ), h ), "", false, theme::Accent, tip( "Clear every layer" ), &kCloseIcon, BoxStyle::Quiet ) )
			for( int l = 0; l < layerCount; ++l )
				app.ClearLayer( l );
		MapTarget( app, "/composition/stop" );
		x += Ui( 24 );
		static const Icon eye = Icon::Eye, eyeOff = Icon::EyeOff;
		if( BoxButton( "##compBypass", ImVec2( x, cy ), ImVec2( Ui( 28 ), h ), "", comp.bypass, theme::Accent, tip( "Black: the whole output off" ), comp.bypass ? &eyeOff : &eye ) )
			comp.bypass = !comp.bypass;
		MapTarget( app, "/composition/bypassed" );
		x += Ui( 34 );
		HorizontalFader( "##compMaster", ImVec2( x, cy ), ImVec2( Ui( 86 ), h ), &comp.master, 0, 1, 1, "M", nullptr, 0 );
		MapTarget( app, "/composition/master" );
		if( ImGui::IsItemHovered() && !learning )
			ImGui::SetTooltip( "Master of the whole composition: %.0f %%\nRight-click: 100 %%", comp.master * 100 );
		x += Ui( 94 );
		char speedText[ 16 ];
		std::snprintf( speedText, sizeof( speedText ), "%.2f\xC3\x97", comp.speed );
		HorizontalFader( "##compSpeed", ImVec2( x, cy ), ImVec2( Ui( 124 ), h ), &comp.speed, 0, 4, 1, "S", speedText, Ui( 44 ) );
		MapTarget( app, "/composition/speed" );
		if( ImGui::IsItemHovered() && !learning )
			ImGui::SetTooltip( "Speed of the whole composition\nRight-click: 1x" );
		// Right-aligned: every clip backwards, pause, forwards; previous and next column.
		float b = Ui( 26 ), right = xEnd - Ui( 6 );
		float xr = right - 5 * b - 2 * Ui( 3 ) - Ui( 8 );
		if( IconBox( "##compReverse", ImVec2( xr, cy ), ImVec2( b, h ), Icon::Reverse, false, tip( "Every playing clip backwards" ), BoxStyle::Quiet ) )
			app.SetAllDirections( -1 );
		if( IconBox( "##compPause", ImVec2( xr + b + Ui( 3 ), cy ), ImVec2( b, h ), Icon::Pause, comp.paused, tip( "Pause everything (Space)" ), BoxStyle::Quiet ) )
			comp.paused = !comp.paused;
		MapTarget( app, "/composition/pause" );
		if( IconBox( "##compPlay", ImVec2( xr + 2 * ( b + Ui( 3 ) ), cy ), ImVec2( b, h ), Icon::Play, false, tip( "Every playing clip forwards" ), BoxStyle::Quiet ) )
			app.SetAllDirections( 1 );
		if( IconBox( "##prevColumn", ImVec2( right - 2 * b - Ui( 3 ), cy ), ImVec2( b, h ), Icon::Previous, false, tip( "Previous column" ) ) )
			app.LaunchNextColumn( -1 );
		MapTarget( app, "/composition/connectprevcolumn" );
		if( IconBox( "##nextColumn", ImVec2( right - b, cy ), ImVec2( b, h ), Icon::Next, false, tip( "Next column" ) ) )
			app.LaunchNextColumn( 1 );
		MapTarget( app, "/composition/connectnextcolumn" );
		hline( origin.y + g.top, x0, xEnd );
	}

	if( !layersPart )
	{
		// Column headers, level with the composition's row: launch, number, name.
		const float y = origin.y;
		ImGui::PushClipRect( ImVec2( cellsLeft, viewMin.y ), viewMax, true );
		for( int c = 0; c < comp.ColumnCount(); ++c )
		{
			ImVec2 pos( origin.x + c * ( cellW + g.hair ), y );
			ImGui::SetCursorScreenPos( pos );
			ImGui::PushID( c );
			bool clicked = ImGui::InvisibleButton( "##column", ImVec2( cellW, g.top ) );
			bool hovered = ImGui::IsItemHovered();
			bool active  = comp.activeColumn == c;
			ImVec2 max( pos.x + cellW, pos.y + g.top );
			if( hovered )
				draw->AddRectFilled( pos, max, theme::Raised );
			float b = Ui( 24 ), by = pos.y + ( g.top - b ) / 2;
			ImVec2 p0( pos.x + Ui( 6 ), by );
			draw->AddRectFilled( p0, ImVec2( p0.x + b, by + b ), active ? theme::Accent : hovered ? theme::RaisedHover : theme::Raised );
			DrawIcon( draw, Icon::Play, ImVec2( p0.x + b * 0.15f, by + b * 0.15f ), ImVec2( p0.x + b * 0.85f, by + b * 0.85f ), active ? theme::OnAccent : theme::TextSoft );
			char number[ 16 ];
			std::snprintf( number, sizeof( number ), "%d", c + 1 );
			float tx = p0.x + b + Ui( 6 );
			ImGui::PushFont( Fonts().mono );
			ImVec2 nt = ImGui::CalcTextSize( number );
			draw->AddText( ImVec2( tx, pos.y + ( g.top - nt.y ) / 2 ), theme::TextFaint, number );
			ImGui::PopFont();
			tx += nt.x + Ui( 5 );
			ImGui::PushFont( Fonts().bold );
			draw->PushClipRect( ImVec2( tx, pos.y ), ImVec2( max.x - Ui( 4 ), max.y ), true );
			draw->AddText( ImVec2( tx, pos.y + ( g.top - ImGui::GetTextLineHeight() ) / 2 ), active ? theme::Accent : theme::Text, comp.columns[ c ].c_str() );
			draw->PopClipRect();
			ImGui::PopFont();
			if( active )
				draw->AddRectFilled( ImVec2( pos.x, max.y - Ui( 2 ) ), max, theme::Accent );
			if( app.IsQueued( -1, c ) )
				DashedRect( draw, ImVec2( pos.x + Ui( 1 ), pos.y + Ui( 1 ) ), ImVec2( max.x - Ui( 1 ), max.y - Ui( 1 ) ), theme::Queued, Ui( 2 ), Ui( 4 ) );
			vline( max.x, pos.y, max.y );
			if( clicked )
				app.LaunchColumn( c );
			if( hovered && !learning && ImGui::IsItemHovered( ImGuiHoveredFlags_DelayNormal ) )
				ImGui::SetTooltip( "Launch column %d: every layer's clip in it\nRight-click: rename", c + 1 );
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
			static const Icon plus = Icon::Plus;
			float b = Ui( 26 );
			ImVec2 pos( origin.x + comp.ColumnCount() * ( cellW + g.hair ) + Ui( 6 ), y + ( g.top - b ) / 2 );
			if( BoxButton( "##addColumn", pos, ImVec2( b, b ), "", false, theme::Accent, "New column", &plus ) )
				comp.AddColumn();
		}
		ImGui::PopClipRect();
		hline( origin.y + g.top, cellsLeft, origin.x + comp.ColumnCount() * ( cellW + g.hair ) );
	}

	// Layers, top layer first.
	for( int row = 0; row < layerCount; ++row )
	{
		int l        = layerCount - 1 - row;
		Layer& layer = comp.layers[ l ];
		const float y = rowsTop + row * ( g.row + g.hair );
		ImGui::PushID( l );
		std::string layerAddress = "/composition/layers/" + std::to_string( l + 1 );
		bool selectedLayer       = comp.selectedLayer == l;
		Clip* playing            = comp.ActiveClip( l );
		auto selectLayer         = [ & ]() {
			comp.selectedLayer = l;
			ImGui::SetWindowFocus( panel::Layer );
		};
		if( layersPart )
		{
			const bool off = layer.bypass;
			// The whole header, under its controls: a click on a free part of it selects the layer.
			ImGui::SetCursorScreenPos( ImVec2( x0, y ) );
			ImGui::SetNextItemAllowOverlap();
			if( ImGui::InvisibleButton( "##header", ImVec2( g.headerW, g.row ) ) )
				selectLayer();
			ImGui::OpenPopupOnItemClick( "##layerMenu", ImGuiPopupFlags_MouseButtonRight );
			if( AcceptEffectDrop( app, layer.effects ) )
				comp.selectedLayer = l;
			if( selectedLayer )
			{
				draw->AddRectFilled( ImVec2( x0, y ), ImVec2( xEnd, y + g.row ), theme::Raised );
				draw->AddRectFilled( ImVec2( x0, y ), ImVec2( x0 + Ui( 2 ), y + g.row ), theme::Accent );
			}
			vline( x1, y, y + g.row );
			vline( x2, y, y + g.row );
			vline( x3, y, y + g.row );
			hline( y + g.row, x0, xEnd );

			// LAYER: number, name and clear; black (bypass) and solo; the blend mode.
			{
				const float lx = x0 + g.pad, lw = g.layerW - 2 * g.pad, ly = y + g.pad;
				char number[ 16 ];
				std::snprintf( number, sizeof( number ), "%d", l + 1 );
				ImGui::PushFont( Fonts().mono );
				ImVec2 nt = ImGui::CalcTextSize( number );
				draw->AddText( ImVec2( lx, ly + ( Ui( 22 ) - nt.y ) / 2 ), theme::TextFaint, number );
				ImGui::PopFont();
				float nx = lx + nt.x + Ui( 5 ), clearW = Ui( 18 );
				ImGui::SetCursorScreenPos( ImVec2( nx, ly ) );
				if( ImGui::InvisibleButton( "##name", ImVec2( lx + lw - clearW - nx, Ui( 22 ) ) ) )
					selectLayer();
				ImGui::OpenPopupOnItemClick( "##layerMenu", ImGuiPopupFlags_MouseButtonRight );
				if( ImGui::IsItemHovered( ImGuiHoveredFlags_DelayNormal ) && !learning )
					ImGui::SetTooltip( "%s: click to select it (its settings and effects in the Layer panel); drop effects here.\nRight-click: rename, "
					                   "new layer, delete%s",
					                   layer.name.c_str(), comp.target == ClipTarget::SelectedLayer ? "\nClips launched from any row play on the selected layer" : "" );
				// Right-click on the layer: rename it, add one, clear or delete it.
				bool deleted = false;
				if( ImGui::BeginPopup( "##layerMenu" ) )
				{
					ImGui::TextDisabled( "%s", layer.name.c_str() );
					ImGui::SetNextItemWidth( Ui( 180 ) );
					ImGui::InputText( "##rename", &layer.name );
					ImGui::Separator();
					if( ImGui::MenuItem( "New Layer" ) )
						comp.AddLayer();
					if( ImGui::MenuItem( "Clear Layer" ) )
						app.ClearLayer( l );
					if( ImGui::MenuItem( "Delete Layer", nullptr, false, comp.layers.size() > 1 ) )
						deleted = true;
					ImGui::EndPopup();
				}
				if( deleted )
				{
					// The rest of this row would draw a layer that is gone: the deck is drawn again next frame.
					app.DeleteLayer( l );
					ImGui::PopID();
					ImGui::EndChild();
					ImGui::End();
					return;
				}
				ImGui::PushFont( Fonts().bold );
				draw->PushClipRect( ImVec2( nx, ly ), ImVec2( lx + lw - clearW, ly + Ui( 22 ) ), true );
				draw->AddText( ImVec2( nx, ly + ( Ui( 22 ) - ImGui::GetTextLineHeight() ) / 2 ), off ? theme::TextDim : selectedLayer ? theme::Accent : theme::Text, layer.name.c_str() );
				draw->PopClipRect();
				ImGui::PopFont();
				if( BoxButton( "##clear", ImVec2( lx + lw - clearW, ly + Ui( 2 ) ), ImVec2( clearW, Ui( 18 ) ), "", false, theme::Accent, tip( "Clear the layer" ), &kCloseIcon,
				               BoxStyle::Quiet ) )
					app.ClearLayer( l );
				MapTarget( app, layerAddress + "/clear" );
				static const Icon eye = Icon::Eye, eyeOff = Icon::EyeOff;
				float half = ( lw - Ui( 4 ) ) / 2, y2 = ly + Ui( 22 ) + Ui( 5 );
				if( BoxButton( "##bypass", ImVec2( lx, y2 ), ImVec2( half, Ui( 26 ) ), "", layer.bypass, theme::Accent, tip( "Black: the layer off" ), layer.bypass ? &eyeOff : &eye ) )
					layer.bypass = !layer.bypass;
				MapTarget( app, layerAddress + "/bypassed" );
				if( BoxButton( "##solo", ImVec2( lx + half + Ui( 4 ), y2 ), ImVec2( half, Ui( 26 ) ), "S", layer.solo, theme::Solo, tip( "Solo: only this layer" ) ) )
					layer.solo = !layer.solo;
				MapTarget( app, layerAddress + "/solo" );
				float y3 = y2 + Ui( 26 ) + Ui( 5 );
				if( DropBox( "##blend", ImVec2( lx, y3 ), ImVec2( lw, Ui( 26 ) ), BlendModeShort( layer.blend ), nullptr,
				             tip( "Blend mode: how this layer mixes with the layers below it" ) ) )
					ImGui::OpenPopup( "##blendMenu" );
				if( ImGui::BeginPopup( "##blendMenu" ) )
				{
					for( int b = 0; b < (int)BlendMode::Count; ++b )
						if( ImGui::Selectable( BlendModeName( (BlendMode)b ), (int)layer.blend == b ) )
							layer.blend = (BlendMode)b;
					ImGui::EndPopup();
				}
			}

			// FADERS: video, audio, master (the most used first); the crossfader group under them.
			{
				const float fx = x1 + g.pad, fwAll = g.fadersW - 2 * g.pad, fy = y + g.pad;
				const float abH = Ui( 18 ), fh = g.row - 2 * g.pad - abH - Ui( 4 );
				const float fw = ( fwAll - 2 * Ui( 4 ) ) / 3;
				char shown[ 16 ];
				float shownOpacity = layer.ShownOpacity( comp.beat ); // moving with the beat: shown, not edited
				float* opacity     = layer.opacityAnimation.mode == AnimMode::Off ? &layer.opacity : &shownOpacity;
				std::snprintf( shown, sizeof( shown ), "%.0f", *opacity * 100 );
				VerticalFader( "##video", ImVec2( fx, fy ), ImVec2( fw, fh ), opacity, "V", shown, "Video", learning, off );
				MapTarget( app, layerAddress + "/video/opacity" );
				if( layer.volume <= 0.0001f )
					std::snprintf( shown, sizeof( shown ), "\xE2\x88\x92\xE2\x88\x9E" ); // minus infinity
				else
					std::snprintf( shown, sizeof( shown ), "%.1f", 20 * std::log10( layer.volume ) );
				VerticalFader( "##audio", ImVec2( fx + fw + Ui( 4 ), fy ), ImVec2( fw, fh ), &layer.volume, "A", shown, "Audio", learning, off );
				MapTarget( app, layerAddress + "/audio/volume" );
				std::snprintf( shown, sizeof( shown ), "%.0f", layer.master * 100 );
				VerticalFader( "##master", ImVec2( fx + 2 * ( fw + Ui( 4 ) ), fy ), ImVec2( fw, fh ), &layer.master, "M", shown, "Master (video and sound)", learning, off );
				MapTarget( app, layerAddress + "/master" );
				float half = ( fwAll - Ui( 4 ) ) / 2, ay = fy + fh + Ui( 4 );
				if( BoxButton( "##groupA", ImVec2( fx, ay ), ImVec2( half, abH ), "A", layer.crossfader == 1, theme::Edge, tip( "Crossfader group A" ), nullptr, BoxStyle::Outline,
				               Fonts().small ) )
					layer.crossfader = layer.crossfader == 1 ? 0 : 1;
				if( BoxButton( "##groupB", ImVec2( fx + half + Ui( 4 ), ay ), ImVec2( half, abH ), "B", layer.crossfader == 2, theme::Edge, tip( "Crossfader group B" ), nullptr,
				               BoxStyle::Outline, Fonts().small ) )
					layer.crossfader = layer.crossfader == 2 ? 0 : 2;
			}

			// CLIP ON AIR: its picture with its name (click: select it), a live dot, and its position to scratch.
			{
				const float cx = x2 + Ui( 6 ), cw = g.clipW - Ui( 12 ), cy = y + g.pad;
				const float barH = Ui( 14 ), ph = g.row - 2 * g.pad - barH - Ui( 4 );
				ImVec2 p0( cx, cy ), p1( cx + cw, cy + ph );
				ImGui::SetCursorScreenPos( p0 );
				if( ImGui::InvisibleButton( "##picture", ImVec2( cw, ph ) ) && playing )
				{
					app.ShowDeck( layer.activeDeck ); // its clip may be in another deck
					app.Select( layer.activeRow, layer.active );
				}
				if( ImGui::IsItemHovered( ImGuiHoveredFlags_DelayNormal ) && playing && !learning )
					ImGui::SetTooltip( "%s: click to select it (Clip panel and Preview Monitor)", playing->name.c_str() );
				draw->AddRectFilled( p0, p1, theme::Well );
				GLuint picture = !playing ? 0 : playing->player ? playing->player->Thumbnail().Texture() : playing->generatorThumbnail ? playing->generatorThumbnail->Texture() : 0;
				if( picture )
				{
					ImVec2 offset;
					ImVec2 size = FitSize( ImVec2( cw, ph ), 16.0f / 9.0f, &offset );
					draw->AddImage( TextureId( picture ), ImVec2( p0.x + offset.x, p0.y + offset.y ), ImVec2( p0.x + offset.x + size.x, p0.y + offset.y + size.y ), ImVec2( 0, 1 ),
					                ImVec2( 1, 0 ), off ? IM_COL32( 255, 255, 255, 90 ) : IM_COL32_WHITE );
				}
				if( playing )
				{
					float nameH = ImGui::GetTextLineHeight() + Ui( 6 );
					draw->AddRectFilledMultiColor( p0, ImVec2( p1.x, p0.y + nameH ), IM_COL32( 0, 0, 0, 190 ), IM_COL32( 0, 0, 0, 190 ), IM_COL32( 0, 0, 0, 0 ),
					                               IM_COL32( 0, 0, 0, 0 ) );
					ImGui::PushFont( Fonts().bold );
					draw->PushClipRect( p0, ImVec2( p1.x - Ui( 14 ), p1.y ), true );
					draw->AddText( ImVec2( p0.x + Ui( 5 ), p0.y + Ui( 2 ) ), off ? theme::TextDim : IM_COL32_WHITE, playing->name.c_str() );
					draw->PopClipRect();
					ImGui::PopFont();
					if( !off )
						draw->AddCircleFilled( ImVec2( p1.x - Ui( 7 ), p0.y + Ui( 7 ) ), Ui( 3.5f ), theme::Accent );
				}
				draw->AddRect( p0, p1, IM_COL32( 0, 0, 0, 255 ) );
				PositionBar( "##position", ImVec2( cx, p1.y + Ui( 4 ) ), ImVec2( cw, barH ), playing, off );
			}

			// CLIP CONTROLS: speed; play mode, timing, previous/next clip; backwards, pause, forwards.
			{
				const float tx = x3 + g.pad, tw = g.controlsW - 2 * g.pad, ty = y + g.pad;
				// The layer's speed, or (a preference) the playing clip's own, as in Resolume.
				bool clipSpeed = app.prefs.layerSpeedIsClipSpeed;
				float noClip   = 1.0f;
				float* speed   = clipSpeed ? ( playing ? &playing->speed : &noClip ) : &layer.speed;
				bool synced    = clipSpeed && playing && playing->syncBeats > 0; // BPM Sync: the beats set its pace
				char speedText[ 16 ];
				std::snprintf( speedText, sizeof( speedText ), "%.2f\xC3\x97", *speed );
				ImGui::BeginDisabled( clipSpeed && ( !playing || synced ) );
				HorizontalFader( "##speed", ImVec2( tx, ty ), ImVec2( tw, Ui( 22 ) ), speed, 0, 4, 1, "S", speedText, Ui( 44 ) );
				ImGui::EndDisabled();
				MapTarget( app, layerAddress + "/speed" );
				if( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) && !learning )
					ImGui::SetTooltip( "%s", !clipSpeed ? "Layer speed: times the clip's own (right-click: 1x)"
					                         : synced   ? "Clip speed: this clip follows BPM Sync, its beats set the pace"
					                                    : "Clip speed: the clip playing on this layer (right-click: 1x)" );

				float y2 = ty + Ui( 22 ) + Ui( 5 ), h = Ui( 26 ), dw = Ui( 44 );
				ImGui::BeginDisabled( !playing || !playing->Loaded() );
				Icon modeIcon = PlayModeIcon( playing ? playing->playMode : PlayMode::Loop );
				if( DropBox( "##loop", ImVec2( tx, y2 ), ImVec2( dw, h ), "", &modeIcon,
				             tip( playing ? PlayModeName( playing->playMode ) : "Loop, Bounce, Play Once & Hold, Play Once & Clear" ) ) )
					ImGui::OpenPopup( "##loopMenu" );
				if( ImGui::BeginPopup( "##loopMenu" ) )
				{
					for( int m = 0; m < (int)PlayMode::Count; ++m )
						if( IconSelectable( PlayModeName( (PlayMode)m ), PlayModeIcon( (PlayMode)m ), playing && (int)playing->playMode == m ) && playing )
							playing->playMode = (PlayMode)m;
					ImGui::EndPopup();
				}
				bool sync       = playing && playing->syncBeats > 0;
				Icon timingIcon = sync ? Icon::Note : Icon::Clock;
				if( DropBox( "##timing", ImVec2( tx + dw + Ui( 4 ), y2 ), ImVec2( dw, h ), "", &timingIcon,
				             tip( sync ? "BPM Sync: the clip lasts a number of beats (set them in the Clip panel)" : "Timeline: the clip at its own speed" ) ) )
					ImGui::OpenPopup( "##timingMenu" );
				if( ImGui::BeginPopup( "##timingMenu" ) )
				{
					if( IconSelectable( "Timeline", Icon::Clock, !sync ) && playing )
						SetBpmSync( *playing, false, comp.bpm );
					if( IconSelectable( "BPM Sync", Icon::Note, sync ) && playing )
						SetBpmSync( *playing, true, comp.bpm );
					ImGui::EndPopup();
				}
				ImGui::EndDisabled();
				float aw = Ui( 28 );
				if( IconBox( "##prevClip", ImVec2( tx + tw - 2 * aw - Ui( 4 ), y2 ), ImVec2( aw, h ), Icon::Previous, false, tip( "Previous clip on this layer" ) ) )
					app.LaunchNextClip( l, -1 );
				MapTarget( app, layerAddress + "/connectprevclip" );
				if( IconBox( "##nextClip", ImVec2( tx + tw - aw, y2 ), ImVec2( aw, h ), Icon::Next, false, tip( "Next clip on this layer" ) ) )
					app.LaunchNextClip( l, 1 );
				MapTarget( app, layerAddress + "/connectnextclip" );

				float y3 = y2 + h + Ui( 5 ), third = ( tw - 2 * Ui( 4 ) ) / 3;
				int direction = playing ? playing->direction : 1;
				ImGui::BeginDisabled( !playing || !playing->Loaded() );
				if( IconBox( "##reverse", ImVec2( tx, y3 ), ImVec2( third, h ), Icon::Reverse, playing && direction < 0, tip( "Backwards" ), BoxStyle::Quiet ) && playing )
					playing->direction = -1;
				if( IconBox( "##pause", ImVec2( tx + third + Ui( 4 ), y3 ), ImVec2( third, h ), Icon::Pause, playing && direction == 0, tip( "Pause this clip" ), BoxStyle::Quiet ) && playing )
					playing->direction = 0;
				if( IconBox( "##forward", ImVec2( tx + 2 * ( third + Ui( 4 ) ), y3 ), ImVec2( third, h ), Icon::Play, playing && direction > 0, tip( "Forwards" ), BoxStyle::Quiet ) && playing )
				{
					playing->direction = 1;
					playing->finished  = false;
				}
				ImGui::EndDisabled();
			}
		}

		if( !layersPart )
		{
			// Cells: the picture launches the clip (on air), the name below selects it (Clip panel and Preview Monitor).
			ImGui::PushClipRect( ImVec2( cellsLeft, viewMin.y ), viewMax, true );
			for( int c = 0; c < comp.ColumnCount(); ++c )
			{
				Clip& clip = layer.clips[ c ];
				ImVec2 min( origin.x + c * ( cellW + g.hair ), y );
				ImVec2 max( min.x + cellW, min.y + g.row );
				ImGui::SetCursorScreenPos( min );
				ImGui::PushID( c );
				bool clicked = ImGui::InvisibleButton( "##cell", ImVec2( cellW, g.row ) );
				bool hovered = ImGui::IsItemHovered();
				ImVec2 cellMin = ImGui::GetItemRectMin(), cellMax = ImGui::GetItemRectMax();
				int showingOn  = comp.LayerShowing( l, c ); // the layer playing it: its own, or another one (layer based)
				bool playing   = showingOn >= 0;
				bool selected  = comp.selectedRow == l && comp.selectedColumn == c;
				bool queued    = app.IsQueued( l, c );
				ImVec2 pic0( min.x + pad, min.y + pad ), pic1( max.x - pad, min.y + pad + picH );
				ImVec2 strip0( pic0.x, pic1.y ), strip1( pic1.x, pic1.y + stripH );
				bool overName = ImGui::GetIO().MousePos.y >= pic1.y;
				vline( max.x, min.y, max.y );
				hline( max.y, min.x, max.x + g.hair );
				if( clip.Empty() )
				{
					draw->AddRectFilled( pic0, pic1, hovered ? theme::Raised : theme::Well );
					DashedRect( draw, pic0, pic1, hovered ? theme::Edge : theme::Line, Ui( 1 ), Ui( 3 ) );
					if( hovered )
					{
						// An empty cell under the mouse: the place for a clip.
						ImVec2 c0( ( pic0.x + pic1.x ) / 2, ( pic0.y + pic1.y ) / 2 );
						float r = Ui( 7 );
						draw->AddLine( ImVec2( c0.x - r, c0.y ), ImVec2( c0.x + r, c0.y ), theme::TextDim, Ui( 1.5f ) );
						draw->AddLine( ImVec2( c0.x, c0.y - r ), ImVec2( c0.x, c0.y + r ), theme::TextDim, Ui( 1.5f ) );
					}
				}
				else
				{
					GLuint thumbnail = clip.player ? clip.player->Thumbnail().Texture() : clip.generatorThumbnail ? clip.generatorThumbnail->Texture() : 0;
					draw->AddRectFilled( pic0, pic1, clip.generator || thumbnail ? IM_COL32( 0, 0, 0, 255 ) : IM_COL32( 70, 30, 34, 255 ) );
					if( thumbnail )
						draw->AddImage( TextureId( thumbnail ), pic0, pic1, ImVec2( 0, 1 ), ImVec2( 1, 0 ), IM_COL32( 255, 255, 255, hovered && !overName || playing ? 255 : 215 ) );
					draw->AddRect( pic0, pic1, IM_COL32( 0, 0, 0, 255 ) );
					ImU32 stripColour = playing ? theme::LiveStrip : hovered && overName ? theme::RaisedHover : theme::Raised;
					draw->AddRectFilled( strip0, strip1, stripColour );
					if( selected )
						draw->AddRect( strip0, strip1, theme::Selected, 0, 0, Ui( 1 ) );
					// Badges: effects (top left), how it triggers (top right), launched onto another layer.
					float bx = pic0.x + Ui( 3 );
					if( !clip.effects.empty() )
						bx += Badge( draw, ImVec2( bx, pic0.y + Ui( 3 ) ), "FX", IM_COL32( 0, 0, 0, 190 ), IM_COL32_WHITE, false ) + Ui( 3 );
					if( playing && showingOn != l )
					{
						char onLayer[ 16 ];
						std::snprintf( onLayer, sizeof( onLayer ), "\xE2\x86\x92%d", showingOn + 1 );
						Badge( draw, ImVec2( bx, pic0.y + Ui( 3 ) ), onLayer, theme::Accent, theme::OnAccent, false );
					}
					float rx = pic1.x - Ui( 3 );
					if( clip.trigger != TriggerStyle::Normal )
						rx -= Badge( draw, ImVec2( rx, pic0.y + Ui( 3 ) ), clip.trigger == TriggerStyle::Toggle ? "T" : "P", IM_COL32( 0, 0, 0, 190 ), IM_COL32_WHITE, true ) + Ui( 3 );
					if( clip.ignoreColumn )
						Badge( draw, ImVec2( rx, pic0.y + Ui( 3 ) ), "NO COL", IM_COL32( 0, 0, 0, 190 ), theme::TextSoft, true );
					// The name; queued: a clock and the queue's colour; missing file: red.
					float nx = strip0.x + Ui( 5 );
					ImU32 labelColour = !clip.Playable() ? theme::Error : queued ? theme::Queued : playing ? theme::Text : theme::TextSoft;
					if( queued )
					{
						float s = ImGui::GetTextLineHeight();
						DrawIcon( draw, Icon::Clock, ImVec2( nx - Ui( 2 ), strip0.y + ( stripH - s ) / 2 ), ImVec2( nx - Ui( 2 ) + s, strip0.y + ( stripH + s ) / 2 ), theme::Queued );
						nx += s + Ui( 1 );
					}
					draw->PushClipRect( strip0, ImVec2( strip1.x - Ui( 4 ), strip1.y ), true );
					draw->AddText( ImVec2( nx, strip0.y + ( stripH - ImGui::GetTextLineHeight() ) / 2 ), labelColour, clip.name.c_str() );
					draw->PopClipRect();
					if( playing )
					{
						// On air: an accent frame round the picture, and how far it has played along its bottom.
						float progress = clip.Duration() > 0 ? (float)( clip.time / clip.Duration() ) : 0;
						draw->AddRectFilled( ImVec2( pic0.x, pic1.y - Ui( 3 ) ), ImVec2( pic0.x + ( pic1.x - pic0.x ) * std::clamp( progress, 0.0f, 1.0f ), pic1.y ), theme::Accent );
						draw->AddRect( ImVec2( pic0.x - Ui( 1 ), pic0.y - Ui( 1 ) ), ImVec2( pic1.x + Ui( 1 ), pic1.y + Ui( 1 ) ), theme::Accent, 0, 0, Ui( 2 ) );
					}
					if( hovered && !clip.Playable() && clip.player )
						ImGui::SetTooltip( "%s\n%s", clip.path.c_str(), clip.player->Error().c_str() );
					else if( hovered && clip.IsGenerator() && !clip.generator )
						ImGui::SetTooltip( "Source not installed: %s", clip.generatorId.c_str() );
					else if( hovered && ImGui::GetIO().KeyAlt )
						ImGui::SetTooltip( "%s", clip.path.c_str() );
					else if( hovered && !learning && ImGui::IsItemHovered( ImGuiHoveredFlags_DelayNormal ) )
						ImGui::SetTooltip( "%s", overName                                ? "Click the name: to the Preview Monitor, without launching"
						                         : clip.trigger == TriggerStyle::Toggle ? "Click the picture: on air, or stop it when it plays (Toggle)"
						                         : clip.trigger == TriggerStyle::Piano  ? "Hold the picture: on air while held (Piano)"
						                                                                : "Click the picture: on air" );
				}
				if( queued )
				{
					// Waiting for its beat: a dashed frame in the queue's colour.
					DashedRect( draw, ImVec2( pic0.x - Ui( 1 ), pic0.y - Ui( 1 ) ), ImVec2( pic1.x + Ui( 1 ), pic1.y + Ui( 1 ) ), theme::Queued, Ui( 2 ), Ui( 4 ) );
				}

				if( app.moveMode )
				{
					// Picked clips: tinted and framed. Under a box being dragged: framed thinner. Where the dragged clips
					// would land: an outline, red when that cell holds another clip.
					bool picked = app.IsPicked( l, c );
					bool inBox  = box.active && !clip.Empty() && min.x < boxMax.x && max.x > boxMin.x && min.y < boxMax.y && max.y > boxMin.y;
					if( picked )
					{
						draw->AddRectFilled( min, max, IM_COL32( 214, 120, 255, 60 ) );
						draw->AddRect( ImVec2( min.x + Ui( 1 ), min.y + Ui( 1 ) ), ImVec2( max.x - Ui( 1 ), max.y - Ui( 1 ) ), kMoveColour, 0, 0, Ui( 2.5f ) );
					}
					else if( inBox )
						draw->AddRect( min, max, kMoveColour, 0, 0, Ui( 1.5f ) );
					if( moveTarget.layer >= 0 && app.IsPicked( l - ( moveTarget.layer - moveFrom.layer ), c - ( moveTarget.column - moveFrom.column ) ) )
					{
						bool taken = !clip.Empty() && !picked;
						draw->AddRect( ImVec2( min.x + Ui( 3 ), min.y + Ui( 3 ) ), ImVec2( max.x - Ui( 3 ), max.y - Ui( 3 ) ), taken ? theme::Error : IM_COL32( 255, 255, 255, 230 ), 0, 0,
						               Ui( 2 ) );
					}
					if( boxRelease && inBox && !picked )
						app.TogglePicked( l, c );
					if( ImGui::IsItemActivated() && clip.Empty() )
					{
						// Pressed on an empty cell: a box starts (with Ctrl or Shift it adds to the picks).
						box.active   = true;
						box.additive = io.KeyCtrl || io.KeyShift;
						box.start    = io.MousePos;
						if( !box.additive )
							app.movePicked.clear();
					}
					if( insertPressed && hovered )
					{
						app.TogglePicked( l, c );
						insertHandled = true;
					}
					// (The cell button is not "hovered" while another item is dragged: test the mouse.)
					if( movingPicked && io.MousePos.x >= cellsLeft && ImGui::IsMouseHoveringRect( min, max )
					    && ImGui::IsWindowHovered( ImGuiHoveredFlags_AllowWhenBlockedByActiveItem ) )
						nextMoveTarget = { l, c };
				}

				// A Piano clip plays from the press on its picture until the button is let go (anywhere).
				bool piano = clip.Playable() && clip.trigger == TriggerStyle::Piano;
				if( piano && !app.moveMode && ImGui::IsItemActivated() && io.MousePos.y < pic1.y )
				{
					app.TriggerClip( l, c, true );
					pianoHeld = { l, c };
				}
				if( clicked && app.moveMode )
				{
					if( !clip.Empty() )
						app.TogglePicked( l, c );
				}
				else if( clicked )
				{
					if( clip.Empty() || ImGui::GetIO().MouseClickedPos[ ImGuiMouseButton_Left ].y >= pic1.y )
						app.Select( l, c );
					else if( !piano )
						app.TriggerClip( l, c, true );
				}
				if( !clip.Empty() && ImGui::BeginDragDropSource() )
				{
					CellRef ref{ l, c };
					if( app.moveMode )
					{
						// Dragging a clip moves every picked clip along (it is picked too).
						if( !app.IsPicked( l, c ) )
							app.TogglePicked( l, c );
						ImGui::SetDragDropPayload( kMovePayload, &ref, sizeof( ref ) );
						ImGui::Text( "Moving %d clip%s", (int)app.movePicked.size(), app.movePicked.size() == 1 ? "" : "s" );
					}
					else
					{
						ImGui::SetDragDropPayload( kCellPayload, &ref, sizeof( ref ) );
						ImGui::TextUnformatted( clip.name.c_str() );
					}
					ImGui::EndDragDropSource();
				}
				if( ImGui::BeginDragDropTarget() )
				{
					if( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( kMovePayload ) )
					{
						CellRef ref = *(const CellRef*)payload->Data;
						app.MovePicked( l - ref.layer, c - ref.column );
					}
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
				if( dropPending && dropPoint.x >= std::max( min.x, cellsLeft ) && dropPoint.x < max.x && dropPoint.y >= min.y && dropPoint.y < max.y )
				{
					pendingLoad = { l, c, app.droppedPaths, {} };
					dropPending = false;
				}
				ImGui::PopID();
			}
			ImGui::PopClipRect();
		}
		ImGui::PopID();
	}

	if( !layersPart )
	{
		if( pianoHeld.layer >= 0 && !ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
		{
			app.TriggerClip( pianoHeld.layer, pianoHeld.column, false );
			pianoHeld = { -1, -1 };
		}
		if( app.moveMode )
		{
			moveTarget = nextMoveTarget;
			if( box.active )
			{
				draw->PushClipRect( ImVec2( cellsLeft, viewMin.y ), viewMax, true );
				draw->AddRectFilled( boxMin, boxMax, IM_COL32( 214, 120, 255, 40 ) );
				draw->AddRect( boxMin, boxMax, kMoveColour );
				draw->PopClipRect();
			}
			if( boxRelease )
				box.active = false;
			// Ins with no cell under the mouse: the selected clip.
			if( insertPressed && !insertHandled )
				app.TogglePicked( comp.selectedRow, comp.selectedColumn );
		}
	}

	// The foot, level in both panels: the crossfader and + Layer under the headers; the deck tabs and a line of help
	// under the cells.
	const float footY = bottom, footH = g.footH, bh = Ui( 26 ), by = footY + ( footH - bh ) / 2;
	if( layersPart )
	{
		ImGui::PushFont( Fonts().bold );
		float addW = ImGui::CalcTextSize( "Layer" ).x + Ui( 30 );
		ImGui::PopFont();
		Crossfader( app, ImVec2( x0 + Ui( 6 ), by ), ImVec2( g.headerW - addW - Ui( 18 ), bh ) );
		static const Icon plus = Icon::Plus;
		ImVec2 ap( xEnd - addW - Ui( 6 ), by );
		if( BoxButton( "##addLayer", ap, ImVec2( addW, bh ), "", false, theme::Accent, tip( "New layer, on top" ), nullptr, BoxStyle::Quiet ) )
			comp.AddLayer();
		DrawIcon( draw, plus, ImVec2( ap.x + Ui( 3 ), ap.y + Ui( 3 ) ), ImVec2( ap.x + bh - Ui( 3 ), ap.y + bh - Ui( 3 ) ), theme::TextSoft );
		ImGui::PushFont( Fonts().bold );
		draw->AddText( ImVec2( ap.x + bh - Ui( 1 ), ap.y + ( bh - ImGui::GetTextLineHeight() ) / 2 ), theme::TextSoft, "Layer" );
		ImGui::PopFont();
	}
	else
	{
		float end      = deckTabs( ImVec2( stickyX + Ui( 6 ), by ), bh );
		const char* help = "Picture: on air  \xC2\xB7  Name: to the Preview Monitor  \xC2\xB7  Drop files on a cell";
		ImVec2 t         = ImGui::CalcTextSize( help );
		float hx         = viewMax.x - t.x - Ui( 10 ) - ( ImGui::GetScrollMaxY() > 0 ? ImGui::GetStyle().ScrollbarSize : 0 );
		if( hx > end + Ui( 20 ) )
			draw->AddText( ImVec2( hx, footY + ( footH - t.y ) / 2 ), theme::TextFaint, help );
	}
	ImGui::SetCursorScreenPos( ImVec2( origin.x, footY ) );
	ImGui::Dummy( ImVec2( layersPart ? g.headerW : comp.ColumnCount() * ( cellW + g.hair ) + Ui( 40 ), footH ) );
	ImGui::EndChild();

	if( !layersPart )
	{
		if( pendingLoad.layer >= 0 && !pendingLoad.generatorId.empty() )
			app.LoadGenerator( pendingLoad.layer, pendingLoad.column, pendingLoad.generatorId );
		else if( pendingLoad.layer >= 0 )
			LoadInto( app, pendingLoad.layer, pendingLoad.column, pendingLoad.paths );
		// Dropped anywhere else: fill the selected layer.
		if( dropPending )
			LoadIntoSelected( app, app.droppedPaths );
	}
	ImGui::End();
}

void DrawClipGrid( App& app )
{
	DrawDeck( app, false );
}

void DrawLayersPanel( App& app )
{
	DrawDeck( app, true );
}

void DrawCompositionMonitor( App& app )
{
	ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 0, 0 ) );
	bool open = ImGui::Begin( panel::CompositionMonitor, &app.panels.compositionMonitor );
	ImGui::PopStyleVar();
	if( open )
	{
		app.monitorShown           = true;
		Composition& comp          = app.composition;
		AdvancedOutputSetup& setup = app.LiveOutput();
		bool arrangement           = app.view == App::View::Arrangement;
		using Source               = App::MonitorSource;
		// The layers it can show alone: the deck's, or the timeline's video tracks top first.
		std::vector< std::pair< int, std::string > > layers;
		if( arrangement )
		{
			const std::vector< TimelineTrack >& tracks = comp.timeline.tracks;
			for( int k = (int)tracks.size() - 1; k >= 0; --k )
				if( !tracks[ k ].audio )
					layers.push_back( { k, tracks[ k ].name } );
		}
		else
			for( int k = (int)comp.layers.size() - 1; k >= 0; --k )
				layers.push_back( { k, comp.layers[ k ].name.empty() ? "Layer " + std::to_string( k + 1 ) : comp.layers[ k ].name } );
		auto layerName = [ & ]( int index ) {
			for( const auto& [ k, name ] : layers )
				if( k == index )
					return name;
			return std::string();
		};

		// The buttons, as on Modul8's monitor: what it shows.
		ImGui::SetCursorPos( ImVec2( Ui( 6 ), ImGui::GetCursorPosY() + Ui( 4 ) ) );
		auto button = [ & ]( const char* label, bool on, const char* tip ) {
			ImGui::PushStyleColor( ImGuiCol_Button, on ? ImGui::ColorConvertU32ToFloat4( theme::Accent ) : ImGui::GetStyleColorVec4( ImGuiCol_Button ) );
			ImGui::PushStyleColor( ImGuiCol_Text, on ? ImGui::ColorConvertU32ToFloat4( theme::OnAccent ) : ImGui::GetStyleColorVec4( ImGuiCol_Text ) );
			bool pressed = ImGui::SmallButton( label );
			ImGui::PopStyleColor( 2 );
			if( ImGui::IsItemHovered() )
				ImGui::SetTooltip( "%s", tip );
			return pressed;
		};
		if( button( "Composition", app.monitorSource == Source::Composition, "The composition: what goes to All Screens" ) )
			app.monitorSource = Source::Composition;
		ImGui::SameLine( 0, Ui( 3 ) );
		if( button( "Multiview", app.monitorSource == Source::Multiview,
		            "Every screen of the output side by side (the composition when there is one screen).\nDouble-click a screen to see it alone." ) )
			app.monitorSource = Source::Multiview;
		ImGui::SameLine( 0, Ui( 3 ) );
		int screenCount   = (int)setup.screens.size();
		app.monitorScreen = std::clamp( app.monitorScreen, 0, std::max( 0, screenCount - 1 ) );
		std::string screenLabel = ( app.monitorSource == Source::Screen && screenCount > 0 ? setup.screens[ app.monitorScreen ].name : std::string( "Screen" ) ) + " \xe2\x96\xbc##screen";
		if( button( screenLabel.c_str(), app.monitorSource == Source::Screen, "One screen of the output, as it goes out" ) )
			ImGui::OpenPopup( "##monitorScreen" );
		if( ImGui::BeginPopup( "##monitorScreen" ) )
		{
			for( int i = 0; i < screenCount; ++i )
				if( ImGui::Selectable( ( setup.screens[ i ].name + "##" + std::to_string( i ) ).c_str(), app.monitorSource == Source::Screen && app.monitorScreen == i ) )
				{
					app.monitorSource = Source::Screen;
					app.monitorScreen = i;
				}
			if( screenCount == 0 )
				ImGui::TextDisabled( "No screens" );
			ImGui::EndPopup();
		}
		ImGui::SameLine( 0, Ui( 3 ) );
		std::string shownLayer  = layerName( app.monitorLayer );
		std::string layerLabel  = ( app.monitorSource == Source::Layer && !shownLayer.empty() ? shownLayer : std::string( arrangement ? "Track" : "Layer" ) ) + " \xe2\x96\xbc##layer";
		if( button( layerLabel.c_str(), app.monitorSource == Source::Layer, arrangement ? "One timeline track alone, with its effects" : "One layer alone, with its effects" ) )
			ImGui::OpenPopup( "##monitorLayer" );
		if( ImGui::BeginPopup( "##monitorLayer" ) )
		{
			for( const auto& [ k, name ] : layers )
				if( ImGui::Selectable( ( name + "##" + std::to_string( k ) ).c_str(), app.monitorSource == Source::Layer && app.monitorLayer == k ) )
				{
					app.monitorSource = Source::Layer;
					app.monitorLayer  = k;
				}
			if( layers.empty() )
				ImGui::TextDisabled( arrangement ? "No video tracks" : "No layers" );
			ImGui::EndPopup();
		}

		ImVec2 area = ImGui::GetContentRegionAvail();
		area.y -= ImGui::GetTextLineHeightWithSpacing();
		ImVec2 origin = ImGui::GetCursorScreenPos();
		std::string caption;
		auto fitted = [ & ]( GLuint texture, int width, int height, ImVec2 at, ImVec2 room ) {
			ImVec2 offset;
			ImVec2 size = FitSize( room, (float)std::max( width, 1 ) / std::max( height, 1 ), &offset );
			ImVec2 a( at.x + offset.x, at.y + offset.y ), b( a.x + size.x, a.y + size.y );
			ImDrawList* draw = ImGui::GetWindowDrawList();
			draw->AddRectFilled( a, b, IM_COL32( 0, 0, 0, 255 ) );
			if( texture )
				draw->AddImage( TextureId( texture ), a, b, ImVec2( 0, 1 ), ImVec2( 1, 0 ) );
			return std::pair< ImVec2, ImVec2 >( a, b );
		};
		Source source = app.monitorSource;
		if( source == Source::Multiview && screenCount <= 1 )
			source = Source::Composition;
		if( source == Source::Screen && screenCount == 0 )
			source = Source::Composition;
		if( source == Source::Composition )
		{
			fitted( app.compositor.CompositionTexture(), comp.width, comp.height, origin, area );
			ImGui::Dummy( area );
			AcceptEffectDrop( app, comp.effects );
			char text[ 96 ];
			std::snprintf( text, sizeof( text ), "Composition - %dx%d", comp.width, comp.height );
			caption = text;
		}
		else if( source == Source::Screen )
		{
			const Screen& screen = setup.screens[ app.monitorScreen ];
			fitted( app.outputs.ScreenTexture( (size_t)app.monitorScreen ), screen.width, screen.height, origin, area );
			ImGui::Dummy( area );
			char text[ 160 ];
			std::snprintf( text, sizeof( text ), "%s - %dx%d", screen.name.c_str(), screen.width, screen.height );
			caption = text;
		}
		else if( source == Source::Layer )
		{
			bool known = !shownLayer.empty();
			fitted( known ? app.compositor.SoloTexture() : 0, known ? app.compositor.SoloWidth() : comp.width, known ? app.compositor.SoloHeight() : comp.height,
			        origin, area );
			ImGui::Dummy( area );
			caption = known ? shownLayer + " alone" : std::string( arrangement ? "Choose a track" : "Choose a layer" );
		}
		else
		{
			// Multiview: the screens in a grid, as many columns as fill the area best.
			int n = screenCount, bestColumns = 1;
			float bestSize = 0;
			for( int columns = 1; columns <= n; ++columns )
			{
				int rows   = ( n + columns - 1 ) / columns;
				float cell = std::min( area.x / columns, area.y / rows * 16.0f / 9.0f );
				if( cell > bestSize )
				{
					bestSize    = cell;
					bestColumns = columns;
				}
			}
			int rows = ( n + bestColumns - 1 ) / bestColumns;
			ImVec2 cell( area.x / bestColumns, area.y / rows );
			ImDrawList* draw = ImGui::GetWindowDrawList();
			for( int i = 0; i < n; ++i )
			{
				const Screen& screen = setup.screens[ i ];
				ImVec2 at( origin.x + ( i % bestColumns ) * cell.x + Ui( 2 ), origin.y + ( i / bestColumns ) * cell.y + Ui( 2 ) );
				ImVec2 room( cell.x - Ui( 4 ), cell.y - Ui( 4 ) - ImGui::GetTextLineHeight() );
				auto [ a, b ] = fitted( app.outputs.ScreenTexture( (size_t)i ), screen.width, screen.height, at, room );
				// Its name under it, in its colour.
				draw->AddRectFilled( ImVec2( a.x, b.y + Ui( 4 ) ), ImVec2( a.x + Ui( 8 ), b.y + Ui( 12 ) ), ScreenColour( i ), Ui( 2 ) );
				draw->PushClipRect( ImVec2( a.x, b.y ), ImVec2( b.x, b.y + ImGui::GetTextLineHeight() + Ui( 2 ) ), true );
				draw->AddText( ImVec2( a.x + Ui( 12 ), b.y + Ui( 1 ) ), screen.enabled ? theme::Text : theme::TextDim, screen.name.c_str() );
				draw->PopClipRect();
				ImGui::SetCursorScreenPos( at );
				ImGui::PushID( i );
				ImGui::InvisibleButton( "##tile", ImVec2( std::max( 1.0f, cell.x - Ui( 4 ) ), std::max( 1.0f, cell.y - Ui( 4 ) ) ) );
				if( ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
				{
					app.monitorSource = Source::Screen;
					app.monitorScreen = i;
				}
				if( ImGui::IsItemHovered() )
					draw->AddRect( a, b, theme::Accent );
				ImGui::PopID();
			}
			ImGui::SetCursorScreenPos( ImVec2( origin.x, origin.y + area.y ) );
			caption = "Multiview - " + std::to_string( n ) + " screens (" + ( arrangement ? "Timeline" : "Grid" ) + " setup)";
		}
		ImGui::SetCursorPosX( Ui( 6 ) );
		ImGui::TextDisabled( "%s", caption.c_str() );
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
		ParamLabel( "Audio" );
		PercentSlider( "##audio", &comp.audioVolume );
		MapTarget( app, "/composition/audio/volume" );
		if( ImGui::IsItemHovered() )
			ImGui::SetTooltip( "Master volume of the clips' sound\n%s", app.audio.Status().c_str() );
		ParamLabel( "Speed" );
		FilledSlider( "##speed", &comp.speed, 0.0f, 4.0f, "%.2f" );
		ResetValue( &comp.speed, 1.0f );
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
		if( CommittedInputInts( "##custom", size, 2 ) )
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
		ParamLabel( "Master" );
		PercentSlider( "##master", &layer.master );
		MapTarget( app, layerAddress + "/master" );
		AnimatedLabel( "Opacity", layer.opacityAnimation, 0, 1 );
		float shownOpacity = layer.ShownOpacity( comp.beat );
		PercentSlider( "##opacity", layer.opacityAnimation.mode == AnimMode::Off ? &layer.opacity : &shownOpacity );
		MapTarget( app, layerAddress + "/video/opacity" );
		ParamLabel( "Volume" );
		PercentSlider( "##volume", &layer.volume );
		MapTarget( app, layerAddress + "/audio/volume" );
		ParamLabel( "Speed" );
		FilledSlider( "##speed", &layer.speed, 0.0f, 4.0f, "%.2fx" );
		ResetValue( &layer.speed, 1.0f );
		MapTarget( app, layerAddress + "/speed" );
		ParamLabel( "Blend Mode" );
		EnumCombo( "##blend", &layer.blend, BlendModeName );
		ParamLabel( "Crossfader" );
		const char* groups[] = { "None", "A", "B" };
		ImGui::Combo( "##group", &layer.crossfader, groups, 3 );
		ParamLabel( "Bypass" );
		ImGui::Checkbox( "##bypass", &layer.bypass );
		MapTarget( app, layerAddress + "/bypassed" );
		ParamLabel( "Solo" );
		ImGui::Checkbox( "##solo", &layer.solo );
		MapTarget( app, layerAddress + "/solo" );
		ImGui::EndTable();
	}
	if( ImGui::CollapsingHeader( "Transition", ImGuiTreeNodeFlags_DefaultOpen ) && BeginParams( "##transition" ) )
	{
		// How the next clip launched on this layer takes over from the one playing.
		ParamLabel( "Duration" );
		FilledSlider( "##duration", &layer.transitionDuration, 0, layer.transitionInBeats ? 16.0f : 10.0f, layer.transitionDuration <= 0 ? "Cut" : layer.transitionInBeats ? "%.2f beats" : "%.2f s",
		              ImGuiSliderFlags_AlwaysClamp );
		ResetValue( &layer.transitionDuration, 0 );
		MapTarget( app, "/composition/layers/" + std::to_string( l + 1 ) + "/transition/duration" );
		ParamLabel( "In Beats" );
		if( ImGui::Checkbox( "##beats", &layer.transitionInBeats ) )
		{
			// The same length, in the other unit at the current tempo.
			double factor             = comp.bpm / 60.0;
			layer.transitionDuration = (float)( layer.transitionInBeats ? layer.transitionDuration * factor : layer.transitionDuration / factor );
			layer.transitionDuration = std::clamp( layer.transitionDuration, 0.0f, layer.transitionInBeats ? 16.0f : 10.0f );
		}
		ParamLabel( "Type" );
		EnumCombo( "##type", &layer.transition, TransitionModeName );
		ImGui::EndTable();
	}
	if( ImGui::Button( "Clear Layer" ) )
		app.ClearLayer( l );
	MapTarget( app, "/composition/layers/" + std::to_string( l + 1 ) + "/clear" );
	ImGui::SameLine();
	if( ImGui::Button( "Delete Layer" ) && comp.layers.size() > 1 )
	{
		app.DeleteLayer( l );
		ImGui::End();
		return;
	}
	if( ImGui::CollapsingHeader( "Effects", ImGuiTreeNodeFlags_DefaultOpen ) )
		DrawEffectChain( app, layer.effects, "layerFx", "/composition/layers/" + std::to_string( l + 1 ) );
	ImGui::End();
}

// The clip's timeline, as in Resolume: the range between the in and out triangles under the bar (drag them),
// beat ticks (each beat of a BPM-synced clip, else quarters), cue points (their numbers under the bar drag too),
// and the playhead's triangle on top. Right-click a triangle to reset it, a cue's number to clear it.
// A click or drag anywhere else moves the playhead.
static void Timeline( Clip& clip, float width )
{
	double duration = clip.Duration();
	if( duration <= 0 || width < Ui( 40 ) )
		return;
	const float pad = Ui( 7 ), headH = Ui( 9 ), barH = Ui( 12 ), footH = Ui( 14 );
	ImVec2 p        = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton( "##timeline", ImVec2( width, headH + barH + footH ) );
	ImGuiID id        = ImGui::GetItemID();
	ImGuiStorage* mem = ImGui::GetStateStorage();
	float x0 = p.x + pad, x1 = p.x + width - pad;
	float barTop = p.y + headH, barBottom = barTop + barH;
	auto xAt     = [ & ]( double t ) { return x0 + ( x1 - x0 ) * (float)( t / duration ); };
	double mouseTime = std::clamp( (double)( ImGui::GetIO().MousePos.x - x0 ) / std::max( 1.0f, x1 - x0 ), 0.0, 1.0 ) * duration;
	double start = clip.RangeStart(), end = clip.RangeEnd();
	const double minimum = std::min( duration, 1.0 / 30 );

	// What is under the mouse below the bar: 1 the in point, 2 the out point, 10 + i cue i, else 3 the playhead.
	auto under = [ & ]() {
		float mx = ImGui::GetIO().MousePos.x;
		if( ImGui::GetIO().MousePos.y < barBottom - Ui( 2 ) )
			return 3;
		int grab     = 3;
		float best   = Ui( 8 );
		auto nearest = [ & ]( double t, int what ) {
			float d = std::fabs( mx - xAt( t ) );
			if( d < best )
			{
				best = d;
				grab = what;
			}
		};
		nearest( start, 1 );
		nearest( end, 2 );
		for( int i = 0; i < kCuePoints; ++i )
			if( clip.cues[ i ] >= 0 && clip.cues[ i ] <= duration )
				nearest( clip.cues[ i ], 10 + i );
		return grab;
	};
	if( ImGui::IsItemActivated() )
		mem->SetInt( id, under() );
	if( ImGui::IsItemActive() )
	{
		int grab = mem->GetInt( id, 0 );
		switch( grab )
		{
		case 1: clip.inPoint = std::clamp( mouseTime, 0.0, end - minimum ); break;
		case 2: clip.outPoint = mouseTime >= duration - 1e-3 ? -1.0 : std::clamp( mouseTime, start + minimum, duration ); break;
		case 3: clip.JumpTo( mouseTime ); break;
		default:
			if( grab >= 10 && grab < 10 + kCuePoints )
				clip.cues[ grab - 10 ] = mouseTime;
		}
		start = clip.RangeStart();
		end   = clip.RangeEnd();
	}
	if( ImGui::IsItemHovered() )
	{
		int what = under();
		if( ImGui::IsMouseClicked( ImGuiMouseButton_Right ) )
		{
			if( what == 1 )
				clip.inPoint = 0;
			else if( what == 2 )
				clip.outPoint = -1;
			else if( what >= 10 )
				clip.cues[ what - 10 ] = -1;
			start = clip.RangeStart();
			end   = clip.RangeEnd();
		}
		else if( what == 1 || what == 2 )
			ImGui::SetTooltip( "%s point at %s\nDrag to move it, right-click to reset it", what == 1 ? "In" : "Out", FormatTime( what == 1 ? start : end ).c_str() );
		else if( what >= 10 )
			ImGui::SetTooltip( "Cue %d at %s\nDrag to move it, right-click to delete it", what - 9, FormatTime( clip.cues[ what - 10 ] ).c_str() );
		else
			ImGui::SetTooltip( "%s\nDrag the triangles under the bar to set the in and out points", FormatTime( mouseTime ).c_str() );
	}

	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->AddRectFilled( ImVec2( x0, barTop ), ImVec2( x1, barBottom ), IM_COL32( 20, 22, 26, 255 ), Ui( 3 ) );
	draw->AddRectFilled( ImVec2( xAt( start ), barTop ), ImVec2( xAt( end ), barBottom ), theme::Raised, Ui( 3 ) );
	// Elapsed part of the range.
	if( clip.time > start )
		draw->AddRectFilled( ImVec2( xAt( start ), barTop ), ImVec2( xAt( std::min( clip.time, end ) ), barBottom ), theme::AccentFill, Ui( 3 ) );
	// Ticks: each beat of a synced clip (at most 64), otherwise the quarters of the range.
	int ticks = clip.syncBeats > 0 ? std::min( 64, (int)std::ceil( clip.syncBeats ) ) : 4;
	for( int i = 1; i < ticks; ++i )
	{
		float x = xAt( start + ( end - start ) * i / ticks );
		draw->AddLine( ImVec2( x, barTop + Ui( 2 ) ), ImVec2( x, barBottom - Ui( 2 ) ), theme::Line, 1.0f );
	}
	// Cue points: a mark and its number.
	for( int i = 0; i < kCuePoints; ++i )
	{
		if( clip.cues[ i ] < 0 || clip.cues[ i ] > duration )
			continue;
		float x = xAt( clip.cues[ i ] );
		draw->AddLine( ImVec2( x, barTop ), ImVec2( x, barBottom ), theme::Warning, Ui( 1.5f ) );
		char number[ 4 ];
		std::snprintf( number, sizeof( number ), "%d", i + 1 );
		ImVec2 size = ImGui::CalcTextSize( number );
		draw->AddText( Fonts().mono, ImGui::GetFontSize() * 0.75f, ImVec2( x - size.x * 0.35f, barBottom + Ui( 1 ) ), theme::Warning, number );
	}
	// In and out triangles under the bar, pointing up; the playhead's above it, pointing down.
	float t = Ui( 5 );
	for( double at : { start, end } )
	{
		float x = xAt( at );
		draw->AddTriangleFilled( ImVec2( x, barBottom ), ImVec2( x - t, barBottom + t * 1.6f ), ImVec2( x + t, barBottom + t * 1.6f ), theme::Accent );
	}
	float px = xAt( std::clamp( clip.time, 0.0, duration ) );
	draw->AddTriangleFilled( ImVec2( px - t * 1.2f, barTop - t * 1.6f ), ImVec2( px + t * 1.2f, barTop - t * 1.6f ), ImVec2( px, barTop ), theme::Accent );
	draw->AddLine( ImVec2( px, barTop ), ImVec2( px, barBottom ), theme::Text, Ui( 1.5f ) );
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
		if( !source.IsGpuTexture() && !source.IsStill() && source.IsHardwareDecoded() )
			ImGui::TextColored( ImVec4( 0.95f, 0.7f, 0.3f, 1 ), "Not HAP: decoded by the graphics chip. HAP is still faster to jump around and layer." );
		else if( !source.IsGpuTexture() && !source.IsStill() )
			ImGui::TextColored( ImVec4( 0.95f, 0.7f, 0.3f, 1 ), "Not HAP: decoded on the CPU. Convert to HAP for the best performance." );
	}

	Composition& comp      = app.composition;
	std::string clipAddress = "/composition/layers/" + std::to_string( comp.selectedRow + 1 ) + "/clips/" + std::to_string( comp.selectedColumn + 1 );
	if( clip->Loaded() && !clip->player->Source().IsStill() && ImGui::CollapsingHeader( "Transport", ImGuiTreeNodeFlags_DefaultOpen ) )
	{
		double duration = clip->Duration();
		Timeline( *clip, ImGui::GetContentRegionAvail().x );
		ImGui::TextDisabled( "%s / %s   frame %lld   in %s   out %s", FormatTime( clip->time ).c_str(), FormatTime( duration ).c_str(), (long long)clip->CurrentFrame(),
		                     FormatTime( clip->RangeStart() ).c_str(), FormatTime( clip->RangeEnd() ).c_str() );

		// Direction, loop mode, and how fast: free (speed) or fitted to a number of beats.
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
		ImGui::SetNextItemWidth( Ui( 100 ) );
		EnumCombo( "##playMode", &clip->playMode, PlayModeName );
		ImGui::SameLine();
		bool sync = clip->syncBeats > 0;
		ImGui::SetNextItemWidth( Ui( 110 ) );
		if( ImGui::BeginCombo( "##timing", sync ? "BPM Sync" : "Timeline" ) )
		{
			if( ImGui::Selectable( "Timeline", !sync ) )
				SetBpmSync( *clip, false, comp.bpm );
			if( ImGui::Selectable( "BPM Sync", sync ) )
				SetBpmSync( *clip, true, comp.bpm );
			ImGui::EndCombo();
		}
		if( ImGui::IsItemHovered() )
			ImGui::SetTooltip( "Timeline: plays at its speed. BPM Sync: the clip (between in and out) lasts a number of beats, whatever the tempo." );
		if( BeginParams( "##transport" ) )
		{
			if( sync )
			{
				ParamLabel( "Beats" );
				ImGui::SetNextItemWidth( Ui( 70 ) );
				float beats = clip->syncBeats;
				if( ImGui::DragFloat( "##beats", &beats, 0.05f, 0.25f, 256, "%g" ) )
					clip->syncBeats = std::clamp( beats, 0.25f, 256.0f );
				for( auto [ label, factor, step ] : { std::tuple{ "-", 1.0f, -1.0f }, std::tuple{ "+", 1.0f, 1.0f }, std::tuple{ "/2", 0.5f, 0.0f }, std::tuple{ "x2", 2.0f, 0.0f } } )
				{
					ImGui::SameLine();
					if( ImGui::Button( label ) )
						clip->syncBeats = std::clamp( clip->syncBeats * factor + step, 0.25f, 256.0f );
				}
			}
			else
			{
				ParamLabel( "Speed" );
				FilledSlider( "##speed", &clip->speed, 0.0f, 4.0f, "%.2fx" );
				ResetValue( &clip->speed, 1.0f );
			}
			ImGui::EndTable();
		}

		// Cue points: click an empty one to place it at the playhead, a set one to play from it; its x (or a
		// right-click) deletes it, and the x at the end of the row deletes them all.
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted( "Cues" );
		ImGui::SameLine();
		float spacing = ImGui::GetStyle().ItemSpacing.x * 0.5f;
		bool anyCue   = std::any_of( std::begin( clip->cues ), std::end( clip->cues ), []( double c ) { return c >= 0; } );
		float clearW  = ImGui::GetFrameHeight();
		float width   = ( ImGui::GetContentRegionAvail().x - spacing * kCuePoints - clearW ) / kCuePoints;
		const float crossW = std::min( Ui( 13 ), width * 0.45f );
		for( int i = 0; i < kCuePoints; ++i )
		{
			ImGui::PushID( i );
			if( i > 0 )
				ImGui::SameLine( 0, spacing );
			bool set = clip->cues[ i ] >= 0;
			if( set )
			{
				ImGui::PushStyleColor( ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4( theme::AccentDim ) );
				ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4( theme::Accent ) );
			}
			char label[ 16 ];
			std::snprintf( label, sizeof( label ), set ? "%d##cue" : "%d", i + 1 );
			ImGui::PushStyleVar( ImGuiStyleVar_ButtonTextAlign, ImVec2( set ? 0.25f : 0.5f, 0.5f ) );
			bool pressed = ImGui::Button( label, ImVec2( width, 0 ) );
			ImGui::PopStyleVar();
			ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
			bool onCross = set && ImGui::IsItemHovered() && ImGui::GetIO().MousePos.x >= b.x - crossW;
			if( pressed )
			{
				if( !set )
					clip->cues[ i ] = clip->time;
				else if( ImGui::GetIO().MouseClickedPos[ 0 ].x >= b.x - crossW )
					clip->cues[ i ] = -1;
				else
					app.JumpToCue( comp.selectedRow, comp.selectedColumn, i );
			}
			if( set )
			{
				ImGui::PopStyleColor( 2 );
				// The delete cross on the right of the button.
				ImDrawList* draw = ImGui::GetWindowDrawList();
				ImVec2 c( b.x - crossW * 0.5f, ( a.y + b.y ) * 0.5f );
				float r = Ui( 3 );
				if( onCross )
					draw->AddRectFilled( ImVec2( b.x - crossW, a.y ), b, IM_COL32( 200, 60, 60, 200 ), ImGui::GetStyle().FrameRounding );
				ImU32 col = onCross ? IM_COL32( 255, 255, 255, 255 ) : IM_COL32( 255, 255, 255, 150 );
				draw->AddLine( ImVec2( c.x - r, c.y - r ), ImVec2( c.x + r, c.y + r ), col, Ui( 1.5f ) );
				draw->AddLine( ImVec2( c.x - r, c.y + r ), ImVec2( c.x + r, c.y - r ), col, Ui( 1.5f ) );
			}
			if( set && ImGui::IsItemHovered() && ImGui::IsMouseClicked( ImGuiMouseButton_Right ) )
				clip->cues[ i ] = -1;
			if( ImGui::IsItemHovered() && !app.learning )
			{
				if( onCross )
					ImGui::SetTooltip( "Delete cue %d", i + 1 );
				else if( set )
					ImGui::SetTooltip( "Cue %d at %s\nClick: play from here (map it with Shortcuts > Map Keys or Map MIDI)\nx or right-click: delete it", i + 1,
					                   FormatTime( clip->cues[ i ] ).c_str() );
				else
					ImGui::SetTooltip( "Click: put cue %d here (%s)", i + 1, FormatTime( clip->time ).c_str() );
			}
			MapTarget( app, clipAddress + "/cue/" + std::to_string( i + 1 ) );
			ImGui::PopID();
		}
		ImGui::SameLine( 0, spacing );
		ImGui::BeginDisabled( !anyCue );
		if( IconButton( "##clearCues", Icon::Close, false ) )
			for( double& cue : clip->cues )
				cue = -1;
		ImGui::EndDisabled();
		if( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) && !app.learning )
			ImGui::SetTooltip( anyCue ? "Delete all the cues of this clip" : "No cues to delete" );
	}

	// How its trigger (click, key, MIDI pad, OSC) acts, and whether column launches take it: for every clip, still
	// images and sources too.
	if( BeginParams( "##trigger" ) )
	{
		ParamLabel( "Trigger" );
		ImGui::SetNextItemWidth( Ui( 110 ) );
		EnumCombo( "##trigger", &clip->trigger, TriggerStyleName );
		if( ImGui::IsItemHovered() )
			ImGui::SetTooltip( "Normal: a press launches it.\nToggle: a press launches it, the next one stops it.\nPiano: it plays while the key, pad or "
			                   "mouse button is held, and stops when let go." );
		ImGui::SameLine();
		ImGui::Checkbox( "Ignore column", &clip->ignoreColumn );
		if( ImGui::IsItemHovered() )
			ImGui::SetTooltip( "Launching its column leaves this clip (and its layer) alone: it plays only when launched itself." );
		ImGui::EndTable();
	}

	// Settings that can follow the beat: while one moves its slider shows where it is and takes no edits.
	float W = (float)comp.width, H = (float)comp.height;
	float shownValues[ (int)ClipProperty::Count ];
	auto edited = [ & ]( ClipProperty p ) -> float* {
		if( clip->animations[ (int)p ].mode == AnimMode::Off )
		{
			switch( p )
			{
			case ClipProperty::Opacity: return &clip->opacity;
			case ClipProperty::PositionX: return &clip->positionX;
			case ClipProperty::PositionY: return &clip->positionY;
			case ClipProperty::Scale: return &clip->scale;
			default: return &clip->rotation;
			}
		}
		shownValues[ (int)p ] = clip->Shown( p, comp.beat, W, H );
		return &shownValues[ (int)p ];
	};
	auto label = [ & ]( const char* text, ClipProperty p ) {
		float lo, hi;
		Clip::PropertyRange( p, W, H, lo, hi );
		AnimatedLabel( text, clip->animations[ (int)p ], lo, hi );
	};
	if( ImGui::CollapsingHeader( "Video", ImGuiTreeNodeFlags_DefaultOpen ) && BeginParams( "##video" ) )
	{
		label( "Opacity", ClipProperty::Opacity );
		PercentSlider( "##opacity", edited( ClipProperty::Opacity ) );
		ImGui::EndTable();
	}
	if( clip->audio && ImGui::CollapsingHeader( "Audio", ImGuiTreeNodeFlags_DefaultOpen ) && BeginParams( "##audio" ) )
	{
		ParamLabel( "Volume" );
		PercentSlider( "##volume", &clip->volume );
		MapTarget( app, clipAddress + "/audio/volume" );
		ImGui::EndTable();
		ImGui::TextDisabled( "Heard while it plays forwards; the speed changes the pitch." );
	}
	if( ImGui::CollapsingHeader( "Transform", ImGuiTreeNodeFlags_DefaultOpen ) && BeginParams( "##transform" ) )
	{
		if( !clip->generator )
		{
			ParamLabel( "Fit" );
			EnumCombo( "##fit", &clip->fit, FitModeName );
		}
		label( "Position X", ClipProperty::PositionX );
		float* x = edited( ClipProperty::PositionX );
		ImGui::DragFloat( "##x", x, 1.0f, -16384, 16384, "%.0f" );
		ResetValue( x, 0.0f );
		label( "Position Y", ClipProperty::PositionY );
		float* y = edited( ClipProperty::PositionY );
		ImGui::DragFloat( "##y", y, 1.0f, -16384, 16384, "%.0f" );
		ResetValue( y, 0.0f );
		label( "Scale", ClipProperty::Scale );
		float* scaleValue = edited( ClipProperty::Scale );
		float scale       = *scaleValue * 100;
		if( ImGui::DragFloat( "##scale", &scale, 0.5f, 0, 1000, "%.1f %%" ) )
			*scaleValue = scale / 100;
		ResetValue( scaleValue, 1.0f );
		label( "Rotation", ClipProperty::Rotation );
		float* rotation = edited( ClipProperty::Rotation );
		FilledSlider( "##rotation", rotation, -180, 180, "%.1f deg" );
		ResetValue( rotation, 0.0f );
		ParamLabel( "" );
		if( ImGui::Button( "Reset Transform" ) )
		{
			clip->positionX = clip->positionY = clip->rotation = 0;
			clip->scale                                        = 1;
			for( ClipProperty p : { ClipProperty::PositionX, ClipProperty::PositionY, ClipProperty::Scale, ClipProperty::Rotation } )
				clip->animations[ (int)p ] = Animation{};
		}
		ImGui::EndTable();
		ImGui::TextDisabled( "Click a setting's name to animate it to the beat." );
	}
	if( ImGui::CollapsingHeader( "Effects", ImGuiTreeNodeFlags_DefaultOpen ) )
		DrawEffectChain( app, clip->effects, "clipFx", clipAddress );
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
			ImGui::PushStyleColor( ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4( theme::Accent ) );
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
	std::vector< OutputWindow::Display > displays = OutputWindow::Displays();

	// The mode: four buttons side by side, the one in use lit.
	ImGui::SeparatorText( "Show the composition" );
	float spacing = ImGui::GetStyle().ItemSpacing.x;
	float width   = ( ImGui::GetContentRegionAvail().x - spacing * 3 ) / 4;
	auto modeButton = [ & ]( const char* label, bool on ) {
		if( on )
		{
			ImGui::PushStyleColor( ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4( theme::Accent ) );
			ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4( theme::Accent ) );
			ImGui::PushStyleColor( ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4( theme::OnAccent ) );
		}
		bool clicked = ImGui::Button( label, ImVec2( width, 0 ) );
		if( on )
			ImGui::PopStyleColor( 3 );
		return clicked;
	};
	if( modeButton( "Off", app.outputMode == OutputMode::Off ) )
		app.SetOutputMode( OutputMode::Off );
	ImGui::SameLine();
	if( modeButton( "Window", app.outputMode == OutputMode::Window ) )
		app.SetOutputMode( OutputMode::Window );
	ImGui::SameLine();
	if( modeButton( "Fullscreen", app.outputMode == OutputMode::Fullscreen ) )
		ImGui::OpenPopup( "##fullscreenOn" );
	if( ImGui::BeginPopup( "##fullscreenOn" ) )
	{
		ImGui::TextDisabled( "Fullscreen on" );
		for( const OutputWindow::Display& d : displays )
		{
			std::string label = d.name + "  " + std::to_string( d.width ) + "x" + std::to_string( d.height ) + " @ " + std::to_string( d.refresh ) + " Hz";
			if( ImGui::MenuItem( label.c_str(), nullptr, app.outputMode == OutputMode::Fullscreen && OutputWindow::FindDisplay( app.outputDisplay ) == d.monitor ) )
				app.SetOutputMode( OutputMode::Fullscreen, d.id );
		}
		ImGui::EndPopup();
	}
	ImGui::SameLine();
	if( modeButton( "Advanced", app.outputMode == OutputMode::Advanced ) )
		app.ToggleAdvancedOutput();
	if( ImGui::IsItemHovered() && app.outputMode == OutputMode::Advanced )
		ImGui::SetTooltip( "Press again to go back to the output before Advanced" );
	ImGui::PushTextWrapPos( 0 );
	switch( app.outputMode )
	{
	case OutputMode::Off: ImGui::TextDisabled( "Nothing is shown outside this window." ); break;
	case OutputMode::Window:
	case OutputMode::Fullscreen: ImGui::TextDisabled( "The whole composition - %s. Escape on the output turns it off.", app.quickOutputs.DeviceStatus( 0 ).c_str() ); break;
	case OutputMode::Advanced:
		if( std::none_of( app.LiveOutput().screens.begin(), app.LiveOutput().screens.end(), []( const Screen& s ) { return s.device != OutputDevice::Virtual; } ) )
			ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( theme::Warning ), "Every screen is a Virtual Output, so nothing is shown: choose a display or Window for a screen below." );
		else
			ImGui::TextDisabled( "The screens below, with their slices, warps and masks." );
		break;
	}
	ImGui::PopTextWrapPos();

	// The Advanced Output setup: kept whatever the mode, shown in Advanced.
	ImGui::SeparatorText( "Advanced Output" );
	if( ImGui::Button( "Advanced Output Settings...", ImVec2( -FLT_MIN, 0 ) ) )
		app.panels.advancedOutput = true;
	bool advanced = app.outputMode == OutputMode::Advanced;
	if( !advanced )
		ImGui::TextDisabled( "Not in use: press Advanced above to show it." );
	for( size_t s = 0; s < app.LiveOutput().screens.size(); ++s )
	{
		Screen& screen = app.LiveOutput().screens[ s ];
		ImGui::PushID( (int)s );
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted( screen.name.c_str() );
		ImGui::SameLine( Ui( 90 ) );
		std::string current = screen.device == OutputDevice::Display ? OutputWindow::DisplayLabel( screen.display ) : OutputDeviceName( screen.device );
		ImGui::SetNextItemWidth( -FLT_MIN );
		if( ImGui::BeginCombo( "##device", current.c_str() ) )
		{
			if( ImGui::Selectable( "Virtual Output (not shown)", screen.device == OutputDevice::Virtual ) )
				app.ShowScreenOn( s, OutputDevice::Virtual );
			if( ImGui::Selectable( "Window", screen.device == OutputDevice::Window ) )
				app.ShowScreenOn( s, OutputDevice::Window );
			for( const OutputWindow::Display& d : displays )
			{
				std::string label = "Fullscreen " + d.name + "  " + std::to_string( d.width ) + "x" + std::to_string( d.height ) + " @ " + std::to_string( d.refresh ) + " Hz";
				if( ImGui::Selectable( label.c_str(), screen.device == OutputDevice::Display && OutputWindow::FindDisplay( screen.display ) == d.monitor ) )
					app.ShowScreenOn( s, OutputDevice::Display, d.id );
			}
			ImGui::EndCombo();
		}
		ImGui::TextDisabled( "%dx%d, %d slice%s%s%s", screen.width, screen.height, (int)screen.slices.size(), screen.slices.size() == 1 ? "" : "s",
		                     advanced ? " - " : "", advanced ? app.outputs.DeviceStatus( s ).c_str() : "" );
		ImGui::PopID();
	}
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
	if( app.FramePeriod() > 0 )
	{
		// Whether clips move in even steps with the display (see App::FrameStep).
		ImVec4 colour = ImGui::ColorConvertU32ToFloat4( app.ClockLocked() ? theme::Accent : theme::Warning );
		ImGui::TextColored( colour, "Display %.2f Hz: %s", 1.0 / app.FramePeriod(),
		                    app.ClockLocked() ? "frames locked to its refresh (even motion)" : "no steady vsync, frames follow the clock" );
	}
	ImGui::TextDisabled( "GPU: %s", Caps().renderer );
	ImGui::TextDisabled( "OpenGL %s   DXT %s   BPTC %s   decode threads %u", Caps().version, Caps().s3tc ? "yes" : "NO", Caps().bptc ? "yes" : "no",
	                     ThreadPool::Shared().WorkerCount() + 1 );
	ImGui::TextDisabled( "Audio: %s", app.audio.Status().c_str() );
	if( HasFfmpeg() )
	{
		bool hardware = HardwareDecoding();
		if( ImGui::Checkbox( "Hardware decoding (H.264, HEVC, VP9...)", &hardware ) )
		{
			app.prefs.hardwareDecoding = hardware;
			app.SavePreferences();
			SetHardwareDecoding( hardware );
			app.SetStatus( hardware ? "Clips loaded from now on use the video hardware when they can" : "Clips loaded from now on decode on the CPU" );
		}
		if( ImGui::IsItemHovered() )
			ImGui::SetTooltip( "The graphics chip decodes non-HAP video (VideoToolbox, D3D11, VA-API), sparing the CPU.\nApplies to clips loaded from now on." );
	}

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
			Layer& layer  = app.composition.layers[ l ];
			Clip* playing = app.composition.ActiveClip( l );
			if( !playing || !playing->Loaded() )
				continue;
			Clip& clip              = *playing;
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

// For a moment after a zoom: the interface's new size, in the middle of the window.
static void DrawZoomToast( App& app )
{
	if( glfwGetTime() > app.zoomShownUntil )
		return;
	char text[ 64 ];
	std::snprintf( text, sizeof( text ), "Interface  %.0f %%", app.prefs.interfaceScale * 100 );
	const char* hint     = "Ctrl 0: 100 %";
	ImGuiViewport* view  = ImGui::GetMainViewport();
	ImDrawList* draw     = ImGui::GetForegroundDrawList( view );
	ImGui::PushFont( Fonts().bold );
	ImVec2 t = ImGui::CalcTextSize( text );
	ImGui::PopFont();
	ImVec2 h = ImGui::CalcTextSize( hint );
	float w  = t.x + h.x + Ui( 40 ), height = t.y + Ui( 16 );
	ImVec2 a( view->Pos.x + ( view->Size.x - w ) / 2, view->Pos.y + Ui( 90 ) );
	draw->AddRectFilled( a, ImVec2( a.x + w, a.y + height ), theme::Raised );
	draw->AddRect( a, ImVec2( a.x + w, a.y + height ), theme::Edge );
	draw->AddText( Fonts().bold, Fonts().bold->FontSize, ImVec2( a.x + Ui( 14 ), a.y + Ui( 8 ) ), theme::Text, text );
	draw->AddText( ImVec2( a.x + Ui( 26 ) + t.x, a.y + ( height - h.y ) / 2 ), theme::TextDim, hint );
}

void DrawStatusBar( App& app )
{
	DrawZoomToast( app );
	ImGui::SetCursorPosX( Ui( 8 ) );
	ImGui::AlignTextToFramePadding();
	bool fresh = glfwGetTime() - app.statusTime < 8.0;
	if( fresh && !app.status.empty() )
		ImGui::TextColored( app.statusIsError ? ImVec4( 0.9f, 0.35f, 0.3f, 1 ) : ImVec4( 0.7f, 0.7f, 0.7f, 1 ), "%s", app.status.c_str() );
	else
		ImGui::TextDisabled( "%s", app.compositionPath.empty() ? "Unsaved composition" : app.compositionPath.c_str() );
	const char* version = "STLT " STLT_VERSION;
	ImGui::SameLine( ImGui::GetWindowWidth() - ImGui::CalcTextSize( version ).x - Ui( 12 ) );
	ImGui::TextDisabled( "%s", version );
}
}
