#include "ui/AdvancedOutputUi.h"
#include <imgui.h>
#include <imgui_stdlib.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include "App.h"
#include "Settings.h"
#include "ui/FileDialogs.h"
#include "ui/Theme.h"
#include "ui/Workspaces.h"

namespace fs = std::filesystem;

namespace stltvj
{
namespace
{
enum Tab
{
	InputTab,
	OutputTab
};

enum class Drag
{
	None,
	Handle,
	Move,
};

struct Editor
{
	int screen = 0;
	int slice  = -1;
	int tab    = InputTab;
	float zoom[ 2 ] = { 1, 1 };
	ImVec2 pan[ 2 ];
	bool snap = true;

	Drag drag     = Drag::None;
	int handle    = -1;
	Vec2 dragFrom;
	std::vector< Vec2 > before; // the edited points when the drag started
	bool snappedX = false, snappedY = false;
	Vec2 snapAt;

	std::string presetName;
};

Editor editor;

float Ui( float pixels )
{
	return pixels * ImGui::GetFontSize() / 15.0f;
}

ImTextureID TextureId( GLuint texture )
{
	return (ImTextureID)(intptr_t)texture;
}

// The points the canvas edits for a slice: its input, or its output (perspective corners, grid, or polygon points).
std::vector< Vec2* > EditablePoints( Slice& s, int tab )
{
	std::vector< Vec2* > points;
	if( tab == InputTab )
		for( Vec2& p : s.input )
			points.push_back( &p );
	else if( s.polygon )
		for( Vec2& p : s.output )
			points.push_back( &p );
	else if( s.warp == WarpMode::Perspective )
		for( Vec2& p : s.corners )
			points.push_back( &p );
	else
		for( Vec2& p : s.grid )
			points.push_back( &p );
	return points;
}

std::vector< Vec2 > Outline( const Slice& s, int tab )
{
	return tab == InputTab ? s.InputOutline() : s.OutputOutline( 24 );
}

bool IsAxisAlignedRect( const std::vector< Vec2 >& p )
{
	return p.size() == 4 && std::fabs( p[ 0 ].y - p[ 1 ].y ) < 0.01f && std::fabs( p[ 3 ].y - p[ 2 ].y ) < 0.01f && std::fabs( p[ 0 ].x - p[ 3 ].x ) < 0.01f
	       && std::fabs( p[ 1 ].x - p[ 2 ].x ) < 0.01f;
}

void SetRect( Slice& s, float x0, float y0, float x1, float y1 )
{
	float l = std::min( x0, x1 ), r = std::max( x0, x1 ), t = std::min( y0, y1 ), b = std::max( y0, y1 );
	s.input = { { l, t }, { r, t }, { r, b }, { l, b } };
}

void Bounds( const std::vector< Vec2 >& points, float& minX, float& minY, float& maxX, float& maxY )
{
	minX = minY = 1e30f;
	maxX = maxY = -1e30f;
	for( Vec2 p : points )
	{
		minX = std::min( minX, p.x );
		minY = std::min( minY, p.y );
		maxX = std::max( maxX, p.x );
		maxY = std::max( maxY, p.y );
	}
}

// Snaps x and y separately to the canvas edges and centre and to the other slices' points.
Vec2 Snap( Vec2 p, const Screen& screen, int tab, int self, float width, float height, float threshold )
{
	editor.snappedX = editor.snappedY = false;
	if( !editor.snap || ImGui::GetIO().KeyCtrl )
		return p;
	std::vector< float > xs = { 0, width / 2, width }, ys = { 0, height / 2, height };
	for( int i = 0; i < (int)screen.slices.size(); ++i )
	{
		if( i == self )
			continue;
		const Slice& s = screen.slices[ i ];
		std::vector< Vec2 > pts = tab == InputTab ? s.input : ( s.polygon ? s.output : s.OutputOutline( 1 ) );
		for( Vec2 q : pts )
		{
			xs.push_back( q.x );
			ys.push_back( q.y );
		}
	}
	float bestX = threshold, bestY = threshold;
	Vec2 out    = p;
	for( float x : xs )
		if( std::fabs( x - p.x ) < bestX )
		{
			bestX           = std::fabs( x - p.x );
			out.x           = x;
			editor.snappedX = true;
		}
	for( float y : ys )
		if( std::fabs( y - p.y ) < bestY )
		{
			bestY           = std::fabs( y - p.y );
			out.y           = y;
			editor.snappedY = true;
		}
	editor.snapAt = out;
	return out;
}

std::string UniqueName( const std::vector< Slice >& slices, const std::string& base )
{
	for( int n = (int)slices.size() + 1;; ++n )
	{
		std::string name = base + " " + std::to_string( n );
		if( std::none_of( slices.begin(), slices.end(), [ & ]( const Slice& s ) { return s.name == name; } ) )
			return name;
	}
}

std::string ResolumeXmlPath()
{
	for( const char* product : { "Resolume Arena", "Resolume Avenue" } )
	{
		fs::path p = fs::u8path( DocumentsDirectory() ) / product / "Preferences" / "AdvancedOutput.xml";
		std::error_code ignored;
		if( fs::exists( p, ignored ) )
			return p.u8string();
	}
	return {};
}

bool SliderPercent( const char* id, float* value, float min, float max )
{
	float percent = *value * 100;
	if( !ImGui::SliderFloat( id, &percent, min * 100, max * 100, "%.0f %%" ) )
		return false;
	*value = percent / 100;
	return true;
}

void Row( const char* label )
{
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex( 0 );
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted( label );
	ImGui::TableSetColumnIndex( 1 );
	ImGui::SetNextItemWidth( -FLT_MIN );
}

bool BeginProps( const char* id )
{
	if( !ImGui::BeginTable( id, 2, ImGuiTableFlags_SizingStretchProp ) )
		return false;
	ImGui::TableSetupColumn( "l", ImGuiTableColumnFlags_WidthFixed, Ui( 92 ) );
	ImGui::TableSetupColumn( "v", ImGuiTableColumnFlags_WidthStretch );
	return true;
}

// ---- left: screens and slices

void DrawList( App& app )
{
	AdvancedOutputSetup& setup = app.advancedOutput;
	Composition& comp          = app.composition;
	float cw = (float)comp.width, ch = (float)comp.height;

	if( ImGui::Button( "+ Screen" ) )
	{
		Screen s;
		s.name   = "Screen " + std::to_string( setup.screens.size() + 1 );
		s.width  = comp.width;
		s.height = comp.height;
		s.slices.push_back( AdvancedOutputSetup::MakeSlice( "Slice 1", cw, ch, (float)s.width, (float)s.height ) );
		setup.screens.push_back( s );
		editor.screen = (int)setup.screens.size() - 1;
		editor.slice  = 0;
	}
	ImGui::SameLine();
	bool haveScreen = editor.screen >= 0 && editor.screen < (int)setup.screens.size();
	ImGui::BeginDisabled( !haveScreen );
	if( ImGui::Button( "+ Slice" ) && haveScreen )
	{
		Screen& s = setup.screens[ editor.screen ];
		s.slices.push_back( AdvancedOutputSetup::MakeSlice( UniqueName( s.slices, "Slice" ), cw, ch, (float)s.width, (float)s.height ) );
		editor.slice = (int)s.slices.size() - 1;
	}
	ImGui::SameLine();
	if( ImGui::Button( "+ Polygon" ) && haveScreen )
	{
		Screen& s = setup.screens[ editor.screen ];
		s.slices.push_back( AdvancedOutputSetup::MakePolygon( UniqueName( s.slices, "Polygon" ), cw, ch, (float)s.width, (float)s.height ) );
		editor.slice = (int)s.slices.size() - 1;
	}
	ImGui::EndDisabled();
	ImGui::Separator();

	ImGui::BeginChild( "##tree", ImVec2( 0, -ImGui::GetFrameHeightWithSpacing() * 6.5f ) );
	for( int si = 0; si < (int)setup.screens.size(); ++si )
	{
		Screen& screen = setup.screens[ si ];
		ImGui::PushID( si );
		ImGui::Checkbox( "##on", &screen.enabled );
		ImGui::SameLine();
		bool screenSelected = editor.screen == si && editor.slice < 0;
		ImGui::SetNextItemOpen( true, ImGuiCond_Once );
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth | ( screenSelected ? ImGuiTreeNodeFlags_Selected : 0 );
		std::string label        = screen.name + "  (" + OutputDeviceName( screen.device ) + ")";
		bool open                = ImGui::TreeNodeEx( "##screen", flags, "%s", label.c_str() );
		if( ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen() )
		{
			editor.screen = si;
			editor.slice  = -1;
		}
		if( open )
		{
			// Top of the list is drawn on top: show the slices last to first.
			for( int k = (int)screen.slices.size() - 1; k >= 0; --k )
			{
				Slice& slice = screen.slices[ k ];
				ImGui::PushID( k );
				ImGui::Checkbox( "##on", &slice.enabled );
				ImGui::SameLine();
				std::string name = ( slice.polygon ? "[P] " : "" ) + slice.name;
				if( ImGui::Selectable( name.c_str(), editor.screen == si && editor.slice == k ) )
				{
					editor.screen = si;
					editor.slice  = k;
				}
				ImGui::PopID();
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
	ImGui::EndChild();

	// Order and copies of the selection.
	haveScreen     = editor.screen >= 0 && editor.screen < (int)setup.screens.size();
	bool haveSlice = haveScreen && editor.slice >= 0 && editor.slice < (int)setup.screens[ editor.screen ].slices.size();
	ImGui::BeginDisabled( !haveSlice );
	if( ImGui::Button( "Up" ) && haveSlice && editor.slice + 1 < (int)setup.screens[ editor.screen ].slices.size() )
	{
		std::swap( setup.screens[ editor.screen ].slices[ editor.slice ], setup.screens[ editor.screen ].slices[ editor.slice + 1 ] );
		editor.slice++;
	}
	ImGui::SameLine();
	if( ImGui::Button( "Down" ) && haveSlice && editor.slice > 0 )
	{
		std::swap( setup.screens[ editor.screen ].slices[ editor.slice ], setup.screens[ editor.screen ].slices[ editor.slice - 1 ] );
		editor.slice--;
	}
	ImGui::SameLine();
	if( ImGui::Button( "Duplicate" ) && haveSlice )
	{
		std::vector< Slice >& slices = setup.screens[ editor.screen ].slices;
		Slice copy                   = slices[ editor.slice ];
		copy.name                    = UniqueName( slices, copy.polygon ? "Polygon" : "Slice" );
		slices.insert( slices.begin() + editor.slice + 1, copy );
		editor.slice++;
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::BeginDisabled( !haveScreen );
	if( ImGui::Button( "Delete" ) && haveScreen )
	{
		if( haveSlice )
		{
			std::vector< Slice >& slices = setup.screens[ editor.screen ].slices;
			slices.erase( slices.begin() + editor.slice );
			editor.slice = std::min( editor.slice, (int)slices.size() - 1 );
		}
		else if( setup.screens.size() > 1 )
		{
			setup.screens.erase( setup.screens.begin() + editor.screen );
			editor.screen = std::max( 0, editor.screen - 1 );
			editor.slice  = -1;
		}
	}
	ImGui::EndDisabled();

	ImGui::SeparatorText( "Presets" );
	std::vector< std::string > presets = app.OutputPresets();
	ImGui::SetNextItemWidth( -FLT_MIN );
	if( ImGui::BeginCombo( "##load", "Load preset...", ImGuiComboFlags_HeightLarge ) )
	{
		for( const std::string& name : presets )
			if( ImGui::Selectable( name.c_str() ) )
			{
				app.LoadOutputPreset( name );
				editor.screen = 0;
				editor.slice  = -1;
			}
		if( presets.empty() )
			ImGui::TextDisabled( "No presets yet" );
		ImGui::EndCombo();
	}
	ImGui::SetNextItemWidth( -Ui( 50 ) );
	ImGui::InputTextWithHint( "##presetName", "preset name", &editor.presetName );
	ImGui::SameLine();
	if( ImGui::Button( "Save" ) )
	{
		if( app.SaveOutputPreset( editor.presetName ) )
			app.SetStatus( "Output preset \"" + editor.presetName + "\" saved" );
		else
			app.SetStatus( "Give the preset a name without / \\ : * ? \" < > |", true );
	}
	std::string resolume = ResolumeXmlPath();
	if( !resolume.empty() && ImGui::Button( "Import from Resolume", ImVec2( -FLT_MIN, 0 ) ) )
	{
		app.ImportResolumeOutput( resolume );
		editor.screen = 0;
		editor.slice  = -1;
	}
	if( ImGui::IsItemHovered() && !resolume.empty() )
		ImGui::SetTooltip( "%s", resolume.c_str() );
	if( ImGui::Button( "Import AdvancedOutput.xml...", ImVec2( -FLT_MIN, 0 ) ) )
		ShowFileDialog( FileDialog::OpenAnyFile, resolume.empty() ? DocumentsDirectory() : resolume, [ &app ]( const std::vector< std::string >& paths ) {
			app.ImportResolumeOutput( paths[ 0 ] );
			editor.screen = 0;
			editor.slice  = -1;
		} );
}

// ---- middle: the canvas

void DrawCanvas( App& app, Screen& screen )
{
	ImGuiIO& io = ImGui::GetIO();
	int tab     = editor.tab;
	float W     = tab == InputTab ? (float)app.composition.width : (float)screen.width;
	float H     = tab == InputTab ? (float)app.composition.height : (float)screen.height;

	ImVec2 pos  = ImGui::GetCursorScreenPos();
	ImVec2 size = ImGui::GetContentRegionAvail();
	size.x      = std::max( size.x, 50.0f );
	size.y      = std::max( size.y, 50.0f );
	ImGui::InvisibleButton( "##canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle );
	bool hovered     = ImGui::IsItemHovered();
	bool focused     = ImGui::IsItemFocused() || ImGui::IsItemActive();
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->PushClipRect( pos, ImVec2( pos.x + size.x, pos.y + size.y ), true );
	draw->AddRectFilled( pos, ImVec2( pos.x + size.x, pos.y + size.y ), IM_COL32( 22, 22, 22, 255 ) );

	float fit   = std::min( size.x / W, size.y / H ) * 0.92f;
	float scale = fit * editor.zoom[ tab ];
	ImVec2 centre( pos.x + size.x / 2 + editor.pan[ tab ].x, pos.y + size.y / 2 + editor.pan[ tab ].y );
	ImVec2 origin( centre.x - W * scale / 2, centre.y - H * scale / 2 );
	auto toScreen  = [ & ]( Vec2 p ) { return ImVec2( origin.x + p.x * scale, origin.y + p.y * scale ); };
	auto toContent = [ & ]( ImVec2 s ) { return Vec2{ ( s.x - origin.x ) / scale, ( s.y - origin.y ) / scale }; };

	// Zoom around the mouse; pan with the middle button or space + drag.
	if( hovered && io.MouseWheel != 0 )
	{
		Vec2 under              = toContent( io.MousePos );
		float zoom              = std::clamp( editor.zoom[ tab ] * std::pow( 1.15f, io.MouseWheel ), 0.1f, 40.0f );
		float newScale          = fit * zoom;
		ImVec2 newOrigin( io.MousePos.x - under.x * newScale, io.MousePos.y - under.y * newScale );
		editor.zoom[ tab ]      = zoom;
		editor.pan[ tab ].x     = newOrigin.x + W * newScale / 2 - ( pos.x + size.x / 2 );
		editor.pan[ tab ].y     = newOrigin.y + H * newScale / 2 - ( pos.y + size.y / 2 );
	}
	bool panning = ( ImGui::IsItemActive() && ImGui::IsMouseDragging( ImGuiMouseButton_Middle ) )
	               || ( ImGui::IsItemActive() && ImGui::IsKeyDown( ImGuiKey_Space ) && ImGui::IsMouseDragging( ImGuiMouseButton_Left ) );
	if( panning )
	{
		editor.pan[ tab ].x += io.MouseDelta.x;
		editor.pan[ tab ].y += io.MouseDelta.y;
	}

	GLuint texture = tab == InputTab ? app.compositor.CompositionTexture() : app.outputs.ScreenTexture( (size_t)editor.screen );
	if( texture )
		draw->AddImage( TextureId( texture ), toScreen( { 0, 0 } ), toScreen( { W, H } ), ImVec2( 0, 1 ), ImVec2( 1, 0 ) );
	draw->AddRect( toScreen( { 0, 0 } ), toScreen( { W, H } ), IM_COL32( 90, 90, 90, 255 ) );

	// Slice outlines; the selected one with its handles (and its grid on the output).
	int selected = editor.slice >= 0 && editor.slice < (int)screen.slices.size() ? editor.slice : -1;
	for( int i = 0; i < (int)screen.slices.size(); ++i )
	{
		const Slice& s           = screen.slices[ i ];
		std::vector< Vec2 > line = Outline( s, tab );
		if( line.size() < 2 )
			continue;
		std::vector< ImVec2 > pts;
		for( Vec2 p : line )
			pts.push_back( toScreen( p ) );
		ImU32 colour = i == selected ? theme::Accent : s.enabled ? IM_COL32( 230, 230, 230, 150 ) : IM_COL32( 140, 140, 140, 90 );
		draw->AddPolyline( pts.data(), (int)pts.size(), colour, ImDrawFlags_Closed, i == selected ? Ui( 2 ) : Ui( 1 ) );
		ImVec2 c( 0, 0 );
		for( ImVec2 p : pts )
		{
			c.x += p.x / pts.size();
			c.y += p.y / pts.size();
		}
		ImVec2 text = ImGui::CalcTextSize( s.name.c_str() );
		draw->AddText( ImVec2( c.x - text.x / 2, c.y - text.y / 2 ), colour, s.name.c_str() );
	}
	if( selected >= 0 )
	{
		Slice& s = screen.slices[ selected ];
		if( tab == OutputTab && !s.polygon && s.warp != WarpMode::Perspective )
		{
			for( int r = 0; r < s.gridRows; ++r )
			{
				std::vector< ImVec2 > row;
				for( int k = 0; k <= 32; ++k )
					row.push_back( toScreen( s.Map( k / 32.0f, (float)r / ( s.gridRows - 1 ) ) ) );
				draw->AddPolyline( row.data(), (int)row.size(), IM_COL32( 102, 225, 192, 110 ), 0, Ui( 1 ) );
			}
			for( int c = 0; c < s.gridColumns; ++c )
			{
				std::vector< ImVec2 > column;
				for( int k = 0; k <= 32; ++k )
					column.push_back( toScreen( s.Map( (float)c / ( s.gridColumns - 1 ), k / 32.0f ) ) );
				draw->AddPolyline( column.data(), (int)column.size(), IM_COL32( 102, 225, 192, 110 ), 0, Ui( 1 ) );
			}
		}
		std::vector< Vec2* > points = EditablePoints( s, tab );
		float handle                = Ui( 4.5f );
		for( size_t k = 0; k < points.size(); ++k )
		{
			ImVec2 p          = toScreen( *points[ k ] );
			bool hot          = editor.drag == Drag::Handle && editor.handle == (int)k;
			ImVec2 d( io.MousePos.x - p.x, io.MousePos.y - p.y );
			bool near         = hovered && d.x * d.x + d.y * d.y < handle * handle * 4;
			draw->AddRectFilled( ImVec2( p.x - handle, p.y - handle ), ImVec2( p.x + handle, p.y + handle ),
			                     hot || near ? IM_COL32( 255, 255, 255, 255 ) : theme::Accent );
			draw->AddRect( ImVec2( p.x - handle, p.y - handle ), ImVec2( p.x + handle, p.y + handle ), IM_COL32( 0, 0, 0, 200 ) );
		}
	}
	if( editor.drag != Drag::None && ( editor.snappedX || editor.snappedY ) )
	{
		ImVec2 p = toScreen( editor.snapAt );
		if( editor.snappedX )
			draw->AddLine( ImVec2( p.x, pos.y ), ImVec2( p.x, pos.y + size.y ), IM_COL32( 255, 200, 80, 160 ) );
		if( editor.snappedY )
			draw->AddLine( ImVec2( pos.x, p.y ), ImVec2( pos.x + size.x, p.y ), IM_COL32( 255, 200, 80, 160 ) );
	}
	draw->PopClipRect();

	// Picking and dragging.
	Vec2 mouse = toContent( io.MousePos );
	if( ImGui::IsItemClicked( ImGuiMouseButton_Left ) && !ImGui::IsKeyDown( ImGuiKey_Space ) )
	{
		editor.drag   = Drag::None;
		editor.handle = -1;
		if( selected >= 0 )
		{
			std::vector< Vec2* > points = EditablePoints( screen.slices[ selected ], tab );
			float best                  = Ui( 9 ) * Ui( 9 );
			for( size_t k = 0; k < points.size(); ++k )
			{
				ImVec2 p = toScreen( *points[ k ] );
				float d  = ( io.MousePos.x - p.x ) * ( io.MousePos.x - p.x ) + ( io.MousePos.y - p.y ) * ( io.MousePos.y - p.y );
				if( d < best )
				{
					best          = d;
					editor.handle = (int)k;
					editor.drag   = Drag::Handle;
				}
			}
		}
		if( editor.drag == Drag::None )
		{
			int hit = -1;
			for( int i = (int)screen.slices.size() - 1; i >= 0 && hit < 0; --i )
				if( PointInPolygon( Outline( screen.slices[ i ], tab ), mouse ) )
					hit = i;
			editor.slice = hit;
			selected     = hit;
			if( hit >= 0 )
				editor.drag = Drag::Move;
		}
		if( editor.drag != Drag::None && selected >= 0 )
		{
			editor.dragFrom = mouse;
			editor.before.clear();
			for( Vec2* p : EditablePoints( screen.slices[ selected ], tab ) )
				editor.before.push_back( *p );
		}
	}
	if( !ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
	{
		editor.drag     = Drag::None;
		editor.snappedX = editor.snappedY = false;
	}
	if( editor.drag != Drag::None && selected >= 0 && ImGui::IsMouseDragging( ImGuiMouseButton_Left, 1.0f ) )
	{
		Slice& s                    = screen.slices[ selected ];
		std::vector< Vec2* > points = EditablePoints( s, tab );
		if( points.size() == editor.before.size() )
		{
			Vec2 delta{ mouse.x - editor.dragFrom.x, mouse.y - editor.dragFrom.y };
			float threshold = Ui( 8 ) / scale;
			if( editor.drag == Drag::Handle && editor.handle >= 0 && editor.handle < (int)points.size() )
			{
				Vec2 target = Snap( { editor.before[ editor.handle ].x + delta.x, editor.before[ editor.handle ].y + delta.y }, screen, tab, selected, W, H, threshold );
				if( tab == InputTab && !s.polygon && !io.KeyAlt && IsAxisAlignedRect( editor.before ) )
				{
					// Rectangles stay rectangles: the opposite corner holds (Alt moves one corner freely).
					Vec2 opposite = editor.before[ ( editor.handle + 2 ) % 4 ];
					SetRect( s, opposite.x, opposite.y, target.x, target.y );
				}
				else
					*points[ editor.handle ] = target;
			}
			else if( editor.drag == Drag::Move )
			{
				if( io.KeyShift )
				{
					if( std::fabs( delta.x ) > std::fabs( delta.y ) )
						delta.y = 0;
					else
						delta.x = 0;
				}
				// Snap the moved shape by its bounding box corners.
				float minX, minY, maxX, maxY;
				Bounds( editor.before, minX, minY, maxX, maxY );
				Vec2 a       = Snap( { minX + delta.x, minY + delta.y }, screen, tab, selected, W, H, threshold );
				bool ax = editor.snappedX, ay = editor.snappedY;
				Vec2 b       = Snap( { maxX + delta.x, maxY + delta.y }, screen, tab, selected, W, H, threshold );
				if( ax )
					delta.x = a.x - minX;
				else if( editor.snappedX )
					delta.x = b.x - maxX;
				if( ay )
					delta.y = a.y - minY;
				else if( editor.snappedY )
					delta.y = b.y - maxY;
				editor.snappedX = ax || editor.snappedX;
				editor.snappedY = ay || editor.snappedY;
				for( size_t k = 0; k < points.size(); ++k )
					*points[ k ] = { editor.before[ k ].x + delta.x, editor.before[ k ].y + delta.y };
			}
		}
	}

	// Arrow keys nudge the selected slice (Shift: 10 pixels).
	if( selected >= 0 && ( hovered || focused ) && !io.WantTextInput )
	{
		float step = io.KeyShift ? 10.0f : 1.0f;
		Vec2 nudge;
		if( ImGui::IsKeyPressed( ImGuiKey_LeftArrow ) )
			nudge.x -= step;
		if( ImGui::IsKeyPressed( ImGuiKey_RightArrow ) )
			nudge.x += step;
		if( ImGui::IsKeyPressed( ImGuiKey_UpArrow ) )
			nudge.y -= step;
		if( ImGui::IsKeyPressed( ImGuiKey_DownArrow ) )
			nudge.y += step;
		if( nudge.x != 0 || nudge.y != 0 )
			for( Vec2* p : EditablePoints( screen.slices[ selected ], tab ) )
			{
				p->x += nudge.x;
				p->y += nudge.y;
			}
	}

	if( ImGui::IsItemClicked( ImGuiMouseButton_Right ) )
	{
		for( int i = (int)screen.slices.size() - 1; i >= 0; --i )
			if( PointInPolygon( Outline( screen.slices[ i ], tab ), mouse ) )
			{
				editor.slice = i;
				break;
			}
		if( editor.slice >= 0 )
			ImGui::OpenPopup( "##sliceMenu" );
	}
	if( ImGui::BeginPopup( "##sliceMenu" ) )
	{
		if( editor.slice >= 0 && editor.slice < (int)screen.slices.size() )
		{
			Slice& s = screen.slices[ editor.slice ];
			ImGui::TextDisabled( "%s", s.name.c_str() );
			if( ImGui::MenuItem( "Match Output to Input" ) )
				s.MatchOutputToInput( (float)app.composition.width, (float)app.composition.height, (float)screen.width, (float)screen.height );
			if( ImGui::MenuItem( "Fit Output to Screen" ) )
				s.FitOutput( (float)screen.width, (float)screen.height );
			if( !s.polygon && ImGui::MenuItem( "Input: Whole Composition" ) )
				SetRect( s, 0, 0, (float)app.composition.width, (float)app.composition.height );
			ImGui::MenuItem( "Flip Horizontally", nullptr, &s.flipHorizontal );
			ImGui::MenuItem( "Flip Vertically", nullptr, &s.flipVertical );
			ImGui::MenuItem( "Enabled", nullptr, &s.enabled );
		}
		ImGui::EndPopup();
	}
}

// ---- right: properties

void DrawScreenProps( App& app, Screen& screen )
{
	ImGui::SeparatorText( "Screen" );
	if( !BeginProps( "##screen" ) )
		return;
	Row( "Name" );
	ImGui::InputText( "##name", &screen.name );
	Row( "Device" );
	std::string current = screen.device == OutputDevice::Display ? screen.display : OutputDeviceName( screen.device );
	if( ImGui::BeginCombo( "##device", current.c_str() ) )
	{
		if( ImGui::Selectable( "Virtual Output", screen.device == OutputDevice::Virtual ) )
			screen.device = OutputDevice::Virtual;
		if( ImGui::Selectable( "Window", screen.device == OutputDevice::Window ) )
			screen.device = OutputDevice::Window;
		for( const OutputWindow::Display& d : OutputWindow::Displays() )
		{
			std::string label = d.name + "  " + std::to_string( d.width ) + "x" + std::to_string( d.height );
			if( ImGui::Selectable( label.c_str(), screen.device == OutputDevice::Display && screen.display == d.monitorName ) )
			{
				screen.device  = OutputDevice::Display;
				screen.display = d.monitorName;
			}
		}
		ImGui::EndCombo();
	}
	Row( "" );
	ImGui::TextDisabled( "%s", app.outputs.DeviceStatus( (size_t)editor.screen ).c_str() );
	Row( "Size" );
	int size[ 2 ] = { screen.width, screen.height };
	if( ImGui::InputInt2( "##size", size, ImGuiInputTextFlags_EnterReturnsTrue ) )
	{
		screen.width  = std::clamp( size[ 0 ], 16, 16384 );
		screen.height = std::clamp( size[ 1 ], 16, 16384 );
	}
	if( screen.device == OutputDevice::Display )
	{
		Row( "" );
		if( ImGui::Button( "Use the Display's Size" ) )
			for( const OutputWindow::Display& d : OutputWindow::Displays() )
				if( d.monitorName == screen.display )
				{
					screen.width  = d.width;
					screen.height = d.height;
				}
	}
	Row( "Enabled" );
	ImGui::Checkbox( "##enabled", &screen.enabled );
	Row( "Test Pattern" );
	ImGui::Checkbox( "##test", &screen.testPattern );
	Row( "Opacity" );
	SliderPercent( "##opacity", &screen.opacity, 0, 1 );
	Row( "Brightness" );
	ImGui::SliderFloat( "##brightness", &screen.brightness, -1, 1, "%.2f" );
	Row( "Contrast" );
	ImGui::SliderFloat( "##contrast", &screen.contrast, -1, 1, "%.2f" );
	Row( "Red" );
	ImGui::SliderFloat( "##red", &screen.red, -1, 1, "%.2f" );
	Row( "Green" );
	ImGui::SliderFloat( "##green", &screen.green, -1, 1, "%.2f" );
	Row( "Blue" );
	ImGui::SliderFloat( "##blue", &screen.blue, -1, 1, "%.2f" );
	ImGui::EndTable();
}

void DrawSliceProps( App& app, Screen& screen, Slice& s )
{
	float cw = (float)app.composition.width, ch = (float)app.composition.height;
	ImGui::SeparatorText( s.polygon ? "Polygon" : "Slice" );
	if( !BeginProps( "##slice" ) )
		return;
	Row( "Name" );
	ImGui::InputText( "##name", &s.name );
	Row( "Enabled" );
	ImGui::Checkbox( "##enabled", &s.enabled );
	Row( "Opacity" );
	SliderPercent( "##opacity", &s.opacity, 0, 1 );

	if( !s.polygon && s.input.size() == 4 )
	{
		float minX, minY, maxX, maxY;
		Bounds( s.input, minX, minY, maxX, maxY );
		float rect[ 4 ] = { minX, minY, maxX - minX, maxY - minY };
		Row( "Input X, Y" );
		bool changed = ImGui::DragFloat2( "##xy", rect, 1, -16384, 16384, "%.0f" );
		Row( "Input W, H" );
		changed |= ImGui::DragFloat2( "##wh", rect + 2, 1, 1, 16384, "%.0f" );
		if( changed )
			SetRect( s, rect[ 0 ], rect[ 1 ], rect[ 0 ] + std::max( rect[ 2 ], 1.0f ), rect[ 1 ] + std::max( rect[ 3 ], 1.0f ) );
		Row( "" );
		if( ImGui::Button( "Whole Composition" ) )
			SetRect( s, 0, 0, cw, ch );
	}
	Row( "Flip" );
	ImGui::Checkbox( "Horizontal", &s.flipHorizontal );
	ImGui::SameLine();
	ImGui::Checkbox( "Vertical", &s.flipVertical );

	if( !s.polygon )
	{
		Row( "Warp" );
		if( ImGui::BeginCombo( "##warp", WarpModeName( s.warp ) ) )
		{
			for( int m = 0; m < (int)WarpMode::Count; ++m )
				if( ImGui::Selectable( WarpModeName( (WarpMode)m ), (int)s.warp == m ) )
					s.SetWarp( (WarpMode)m );
			ImGui::EndCombo();
		}
		if( s.warp != WarpMode::Perspective )
		{
			Row( "Grid Points" );
			int grid[ 2 ] = { s.gridColumns, s.gridRows };
			if( ImGui::SliderInt2( "##grid", grid, 2, 16 ) )
				s.ResampleGrid( grid[ 0 ], grid[ 1 ] );
			Row( "" );
			if( ImGui::Button( "Flatten Grid" ) )
			{
				// Back to the plain perspective shape, keeping the corners.
				WarpMode mode = s.warp;
				s.SetWarp( WarpMode::Perspective );
				s.SetWarp( mode );
			}
		}
	}
	Row( "" );
	if( ImGui::Button( "Match Input" ) )
		s.MatchOutputToInput( cw, ch, (float)screen.width, (float)screen.height );
	ImGui::SameLine();
	if( ImGui::Button( "Fit Screen" ) )
		s.FitOutput( (float)screen.width, (float)screen.height );
	ImGui::EndTable();

	if( ImGui::CollapsingHeader( "Soft Edge", s.edge.Active() ? ImGuiTreeNodeFlags_DefaultOpen : 0 ) && BeginProps( "##edge" ) )
	{
		Row( "Left" );
		SliderPercent( "##left", &s.edge.left, 0, 0.5f );
		Row( "Right" );
		SliderPercent( "##right", &s.edge.right, 0, 0.5f );
		Row( "Top" );
		SliderPercent( "##top", &s.edge.top, 0, 0.5f );
		Row( "Bottom" );
		SliderPercent( "##bottom", &s.edge.bottom, 0, 0.5f );
		Row( "Power" );
		ImGui::SliderFloat( "##power", &s.edge.power, 0.5f, 6, "%.2f" );
		Row( "Luminance" );
		ImGui::SliderFloat( "##luminance", &s.edge.luminance, 0, 1, "%.2f" );
		Row( "Gamma" );
		ImGui::SliderFloat( "##gamma", &s.edge.gamma, 0.5f, 3, "%.2f" );
		ImGui::EndTable();
	}
}
}

void ShowAdvancedOutputTab( int tab, int screen, int slice )
{
	editor.tab    = tab == 1 ? OutputTab : InputTab;
	editor.screen = screen;
	editor.slice  = slice;
}

void DrawAdvancedOutputPanel( App& app )
{
	ImGui::SetNextWindowSize( ImVec2( Ui( 1250 ), Ui( 720 ) ), ImGuiCond_FirstUseEver );
	if( !ImGui::Begin( panel::AdvancedOutput, &app.panels.advancedOutput ) )
	{
		ImGui::End();
		return;
	}
	AdvancedOutputSetup& setup = app.advancedOutput;
	if( setup.screens.empty() )
		setup = AdvancedOutputSetup::MakeDefault( app.composition.width, app.composition.height );
	editor.screen = std::clamp( editor.screen, 0, (int)setup.screens.size() - 1 );
	if( editor.slice >= (int)setup.screens[ editor.screen ].slices.size() )
		editor.slice = -1;

	if( ImGui::BeginTable( "##ao", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, ImGui::GetContentRegionAvail() ) )
	{
		ImGui::TableSetupColumn( "list", ImGuiTableColumnFlags_WidthFixed, Ui( 230 ) );
		ImGui::TableSetupColumn( "canvas", ImGuiTableColumnFlags_WidthStretch );
		ImGui::TableSetupColumn( "props", ImGuiTableColumnFlags_WidthFixed, Ui( 300 ) );
		ImGui::TableNextRow();

		ImGui::TableSetColumnIndex( 0 );
		ImGui::BeginChild( "##left" );
		DrawList( app );
		ImGui::EndChild();

		editor.screen = std::clamp( editor.screen, 0, (int)setup.screens.size() - 1 );
		Screen& screen = setup.screens[ editor.screen ];
		if( editor.slice >= (int)screen.slices.size() )
			editor.slice = -1;

		ImGui::TableSetColumnIndex( 1 );
		ImGui::BeginChild( "##middle" );
		if( ImGui::RadioButton( "Input Selection", editor.tab == InputTab ) )
			editor.tab = InputTab;
		ImGui::SameLine();
		if( ImGui::RadioButton( "Output Transformation", editor.tab == OutputTab ) )
			editor.tab = OutputTab;
		ImGui::SameLine( 0, Ui( 20 ) );
		if( ImGui::Button( "Fit View" ) )
		{
			editor.zoom[ editor.tab ] = 1;
			editor.pan[ editor.tab ]  = ImVec2( 0, 0 );
		}
		ImGui::SameLine();
		ImGui::Text( "%.0f %%", editor.zoom[ editor.tab ] * 100 );
		ImGui::SameLine();
		ImGui::Checkbox( "Snap", &editor.snap );
		ImGui::SameLine();
		ImGui::Checkbox( "Test Pattern", &screen.testPattern );
		ImGui::SameLine();
		ImGui::TextDisabled( "%s - %s %dx%d", screen.name.c_str(), editor.tab == InputTab ? "composition" : "screen",
		                     editor.tab == InputTab ? app.composition.width : screen.width, editor.tab == InputTab ? app.composition.height : screen.height );
		DrawCanvas( app, screen );
		ImGui::EndChild();

		ImGui::TableSetColumnIndex( 2 );
		ImGui::BeginChild( "##right" );
		if( editor.slice >= 0 )
			DrawSliceProps( app, screen, screen.slices[ editor.slice ] );
		DrawScreenProps( app, screen );
		ImGui::Spacing();
		ImGui::TextDisabled( "Drag a slice to move it, its handles to shape it.\nShift: one axis. Alt: free input corner.\nCtrl: no snapping. Arrows: nudge.\nWheel: zoom. Middle button or Space: pan.\nRight-click a slice for more." );
		ImGui::EndChild();
		ImGui::EndTable();
	}
	ImGui::End();
}
}
