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
#include "ui/Widgets.h"
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
	Handle, // a point
	Bezier, // a contour point's Bézier handle
	Scale,  // a transform box handle
	Rotate, // the transform box's rotate knob
	Move,
};

// What dragging on the output does: shape the slice point by point, or move, scale and rotate it whole.
enum class OutputEdit
{
	Points,
	Transform,
};

// How a dragged Bézier handle moves the point's other one.
enum class Link
{
	None,      // not at all
	Opposite,  // stays opposite, keeping its length
	Symmetric, // mirrors it: pulling fresh handles out of a point
};

// The transform box: the bounds of a slice's outline.
struct Box
{
	float minX = 0, minY = 0, maxX = 0, maxY = 0;
	// Scale handles: the corners top-left, top-right, bottom-right, bottom-left, then the middles of the top,
	// right, bottom and left sides.
	Vec2 Handle( int k ) const
	{
		float mx = ( minX + maxX ) / 2, my = ( minY + maxY ) / 2;
		const Vec2 handles[ 8 ] = { { minX, minY }, { maxX, minY }, { maxX, maxY }, { minX, maxY }, { mx, minY }, { maxX, my }, { mx, maxY }, { minX, my } };
		return handles[ k ];
	}
	static int Opposite( int k ) { return k < 4 ? ( k + 2 ) % 4 : 4 + ( k - 2 ) % 4; }
	Vec2 Centre() const { return { ( minX + maxX ) / 2, ( minY + maxY ) / 2 }; }
};

struct Editor
{
	int screen = 0;
	int slice  = -1;
	int tab    = InputTab;
	float zoom[ 2 ] = { 1, 1 };
	ImVec2 pan[ 2 ];
	bool snap = true;

	OutputEdit outputEdit = OutputEdit::Points;
	// The selected point of the selected slice on the current tab (an index into EditablePoints), or -1.
	int point = -1;
	int pointScreen = -1, pointSlice = -1, pointTab = -1;
	int menuPoint      = -1; // the point the context menu is for
	bool menuCanInsert = false;
	Vec2 menuAt;

	Drag drag      = Drag::None;
	int handle     = -1; // the point, the Bézier handle's point, or the box handle being dragged
	bool handleOut = false;
	Link link      = Link::None;
	Box box;
	Vec2 dragFrom;
	std::vector< Vec2 > before;                 // the edited points when the drag started
	std::vector< ContourPoint > beforeContour;  // and the contour, for its handles
	bool snappedX = false, snappedY = false;
	Vec2 snapAt;

	std::string presetName;

	// What the Input tab shows: the selected slice's picture, "" the composition or a canvas's id.
	std::string inputSource;

	// Drawing a new polygon point by point (+ Polygon > Draw): on this screen and tab, the points so far.
	bool drawing   = false;
	int drawScreen = -1, drawTab = InputTab;
	std::vector< Vec2 > drawPoints;
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

// The points the canvas edits for a slice: its input, or its output (perspective corners, grid, contour or
// polygon points).
std::vector< Vec2* > EditablePoints( Slice& s, int tab )
{
	std::vector< Vec2* > points;
	if( s.mask )
	{
		// Masks only live on the output.
		if( tab == OutputTab )
			for( ContourPoint& c : s.contour )
				points.push_back( &c.p );
	}
	else if( tab == InputTab )
		for( Vec2& p : s.input )
			points.push_back( &p );
	else if( s.polygon )
		for( Vec2& p : s.output )
			points.push_back( &p );
	else if( s.warp == WarpMode::Perspective )
		for( Vec2& p : s.corners )
			points.push_back( &p );
	else if( s.warp == WarpMode::Contour )
		for( ContourPoint& c : s.contour )
			points.push_back( &c.p );
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
		if( tab == InputTab && ( s.mask || s.source != editor.inputSource ) )
			continue; // another picture's pixels
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

bool SliderPercent( const char* id, float* value, float min, float max, float reset )
{
	float percent = *value * 100;
	bool changed  = FilledSlider( id, &percent, min * 100, max * 100, "%.0f %%" );
	if( changed )
		*value = percent / 100;
	return ResetValue( value, reset ) || changed;
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
		ImGui::OpenPopup( "##newPolygon" );
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "Any shape of the composition, point by point: draw it, or start from a regular shape" );
	if( ImGui::BeginPopup( "##newPolygon" ) )
	{
		if( ImGui::MenuItem( "Draw..." ) && haveScreen )
		{
			editor.drawing    = true;
			editor.drawScreen = editor.screen;
			editor.drawTab    = editor.tab;
			editor.drawPoints.clear();
		}
		if( ImGui::IsItemHovered() )
			ImGui::SetTooltip( "Click the points on the canvas (Input or Output tab); click the first point, double-click or press "
			                   "Enter to close it" );
		ImGui::Separator();
		struct Shape
		{
			const char* name;
			int sides;
		};
		for( Shape shape : { Shape{ "Triangle", 3 }, Shape{ "Rectangle", 4 }, Shape{ "Pentagon", 5 }, Shape{ "Hexagon", 6 }, Shape{ "Octagon", 8 },
		                     Shape{ "Circle", 32 } } )
			if( ImGui::MenuItem( shape.name ) && haveScreen )
			{
				Screen& s = setup.screens[ editor.screen ];
				s.slices.push_back( AdvancedOutputSetup::MakeRegularPolygon( UniqueName( s.slices, "Polygon" ), shape.sides, cw, ch, (float)s.width, (float)s.height ) );
				editor.slice = (int)s.slices.size() - 1;
			}
		ImGui::EndPopup();
	}
	if( ImGui::Button( "+ Mask" ) && haveScreen )
		ImGui::OpenPopup( "##newMask" );
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "A shape that blacks out what is below it in the list (outside it, inverted)" );
	if( ImGui::BeginPopup( "##newMask" ) )
	{
		for( bool ellipse : { false, true } )
			if( ImGui::MenuItem( ellipse ? "Ellipse" : "Rectangle" ) && haveScreen )
			{
				Screen& s = setup.screens[ editor.screen ];
				s.slices.push_back( AdvancedOutputSetup::MakeMask( UniqueName( s.slices, "Mask" ), (float)s.width, (float)s.height, ellipse ) );
				editor.slice = (int)s.slices.size() - 1;
				editor.tab   = OutputTab; // masks live on the output
			}
		ImGui::EndPopup();
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
				std::string name = ( slice.mask ? "[M] " : slice.polygon ? "[P] " : "" ) + slice.name;
				ImVec2 rowAt     = ImGui::GetCursorScreenPos();
				if( ImGui::Selectable( name.c_str(), editor.screen == si && editor.slice == k ) )
				{
					editor.screen = si;
					editor.slice  = k;
				}
				if( !slice.mask )
				{
					// The colour of what it takes from (the composition, or a canvas), as on the timeline's tracks.
					float h  = ImGui::GetTextLineHeight();
					float x  = ImGui::GetItemRectMax().x - Ui( 10 );
					ImGui::GetWindowDrawList()->AddRectFilled( ImVec2( x, rowAt.y + h * 0.25f ), ImVec2( x + Ui( 6 ), rowAt.y + h * 0.25f + Ui( 6 ) ),
					                                           PictureColour( app, slice.source ), Ui( 2 ) );
					if( ImGui::IsItemHovered() )
						ImGui::SetTooltip( "Takes from: %s", PictureName( app, slice.source ).c_str() );
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
		copy.name                    = UniqueName( slices, copy.mask ? "Mask" : copy.polygon ? "Polygon" : "Slice" );
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

constexpr float kPi = 3.14159265f;

bool EditsContour( const Slice& s, int tab )
{
	return tab == OutputTab && s.HasContour() && s.contour.size() >= ( s.mask ? 3u : 4u );
}

// Points can be added and removed: polygons on both tabs, contours and masks on the output.
bool HasPointEditing( const Slice& s, int tab )
{
	return s.polygon || EditsContour( s, tab );
}

bool Transforming( int tab )
{
	return tab == OutputTab && editor.outputEdit == OutputEdit::Transform;
}

struct BezierHandle
{
	int point;
	bool out;
};

// The Bézier handles shown: the selected contour point's two, and its neighbours' ones facing it.
std::vector< BezierHandle > VisibleHandles( const Slice& s, int tab )
{
	int n = (int)s.contour.size();
	if( !EditsContour( s, tab ) || s.pointMode != PointMode::Bezier || Transforming( tab ) || editor.point < 0 || editor.point >= n )
		return {};
	return { { editor.point, false }, { editor.point, true }, { ( editor.point + n - 1 ) % n, true }, { ( editor.point + 1 ) % n, false } };
}

Vec2 HandlePosition( const Slice& s, BezierHandle h )
{
	const ContourPoint& c = s.contour[ h.point ];
	Vec2 d                = h.out ? c.out : c.in;
	return { c.p.x + d.x, c.p.y + d.y };
}

// Handles a third of the way to each neighbour: both segments through the point are straight.
ContourPoint Straightened( const Slice& s, int i )
{
	int n          = (int)s.contour.size();
	ContourPoint c = s.contour[ i ];
	Vec2 prev = s.contour[ ( i + n - 1 ) % n ].p, next = s.contour[ ( i + 1 ) % n ].p;
	c.in      = { ( prev.x - c.p.x ) / 3, ( prev.y - c.p.y ) / 3 };
	c.out     = { ( next.x - c.p.x ) / 3, ( next.y - c.p.y ) / 3 };
	return c;
}

// Straight points become smooth, anything else straight.
void TogglePointShape( Slice& s, int i )
{
	ContourPoint straight = Straightened( s, i );
	const ContourPoint& c = s.contour[ i ];
	bool isStraight = std::hypot( c.in.x - straight.in.x, c.in.y - straight.in.y ) < 0.5f && std::hypot( c.out.x - straight.out.x, c.out.y - straight.out.y ) < 0.5f;
	if( isStraight )
		s.SmoothContourPoint( i );
	else
		s.contour[ i ] = straight;
}

bool RemovePoint( Slice& s, int tab, int index )
{
	if( s.polygon )
		return s.RemovePolygonPoint( index );
	return EditsContour( s, tab ) && s.RemoveContourPoint( index );
}

int InsertPoint( Slice& s, int tab, Vec2 at, float maxDistance )
{
	if( s.polygon )
		return s.InsertPolygonPoint( tab == OutputTab, at, maxDistance );
	return EditsContour( s, tab ) ? s.InsertContourPoint( at, maxDistance ) : -1;
}

Box OutlineBox( const Slice& s, int tab )
{
	Box box;
	Bounds( Outline( s, tab ), box.minX, box.minY, box.maxX, box.maxY );
	return box;
}

// Remembers the edited points, and the contour's handles, as they are when a drag starts.
void BeginEdit( Slice& s, int tab )
{
	editor.before.clear();
	for( Vec2* p : EditablePoints( s, tab ) )
		editor.before.push_back( *p );
	editor.beforeContour = s.contour;
}

// Places the edited points, and a contour's handles, from where they were when the drag started.
template< typename PointFn, typename VectorFn >
void TransformPoints( Slice& s, int tab, PointFn point, VectorFn vector )
{
	std::vector< Vec2* > points = EditablePoints( s, tab );
	if( points.size() != editor.before.size() )
		return;
	for( size_t k = 0; k < points.size(); ++k )
		*points[ k ] = point( editor.before[ k ] );
	if( EditsContour( s, tab ) && s.contour.size() == editor.beforeContour.size() )
		for( size_t k = 0; k < s.contour.size(); ++k )
		{
			s.contour[ k ].in  = vector( editor.beforeContour[ k ].in );
			s.contour[ k ].out = vector( editor.beforeContour[ k ].out );
		}
}

// + Polygon > Draw: each click on the canvas is a point; clicking the first point, a double-click or Enter makes the
// polygon (three points at least); Backspace takes the last point back, Esc or a right-click on no point gives up.
// Points snap to the other slices as dragged points do. Called with the canvas's clip rectangle pushed; pops it.
template < typename ToScreen, typename ToContent >
void DrawPolygonTool( App& app, Screen& screen, ImDrawList* draw, ImVec2 pos, ImVec2 size, ToScreen toScreen, ToContent toContent, float scale,
                      float W, float H, bool hovered )
{
	ImGuiIO& io              = ImGui::GetIO();
	std::vector< Vec2 >& pts = editor.drawPoints;
	Vec2 mouse               = Snap( toContent( io.MousePos ), screen, editor.drawTab, -1, W, H, Ui( 8 ) / scale );
	const ImU32 amber        = IM_COL32( 255, 190, 70, 255 ), black = IM_COL32( 0, 0, 0, 255 );
	float radius             = Ui( 4.5f );
	auto near                = [ & ]( Vec2 p ) {
        ImVec2 a = toScreen( p );
        return ( io.MousePos.x - a.x ) * ( io.MousePos.x - a.x ) + ( io.MousePos.y - a.y ) * ( io.MousePos.y - a.y ) < radius * radius * 4;
	};
	bool closable = pts.size() >= 3 && near( pts[ 0 ] );

	// What is drawn so far, the next edge to the mouse, and the closing edge when it would close.
	for( size_t i = 1; i < pts.size(); ++i )
		draw->AddLine( toScreen( pts[ i - 1 ] ), toScreen( pts[ i ] ), amber, Ui( 2 ) );
	if( !pts.empty() && hovered )
	{
		draw->AddLine( toScreen( pts.back() ), toScreen( closable ? pts[ 0 ] : mouse ), IM_COL32( 255, 190, 70, 150 ), Ui( 1.5f ) );
		if( pts.size() >= 2 && !closable )
			draw->AddLine( toScreen( mouse ), toScreen( pts[ 0 ] ), IM_COL32( 255, 190, 70, 60 ), Ui( 1 ) );
	}
	for( size_t i = 0; i < pts.size(); ++i )
	{
		ImVec2 p  = toScreen( pts[ i ] );
		float r   = i == 0 && closable ? radius * 1.6f : radius;
		draw->AddRectFilled( ImVec2( p.x - r, p.y - r ), ImVec2( p.x + r, p.y + r ), i == 0 ? IM_COL32( 255, 255, 255, 255 ) : amber );
		draw->AddRect( ImVec2( p.x - r, p.y - r ), ImVec2( p.x + r, p.y + r ), black );
	}
	if( hovered && !closable )
		draw->AddCircle( toScreen( mouse ), radius, amber, 0, Ui( 1.5f ) );
	if( editor.snappedX || editor.snappedY )
	{
		ImVec2 p = toScreen( editor.snapAt );
		if( editor.snappedX )
			draw->AddLine( ImVec2( p.x, pos.y ), ImVec2( p.x, pos.y + size.y ), IM_COL32( 255, 200, 80, 160 ) );
		if( editor.snappedY )
			draw->AddLine( ImVec2( pos.x, p.y ), ImVec2( pos.x + size.x, p.y ), IM_COL32( 255, 200, 80, 160 ) );
	}
	// How it works, along the top of the canvas.
	char hint[ 256 ];
	std::snprintf( hint, sizeof( hint ),
	               pts.size() < 3 ? "Drawing a polygon: click to place its points (%d so far). Esc: cancel"
	                              : "Drawing a polygon (%d points): click the first point, double-click or Enter to finish. Backspace: undo point. Esc: cancel",
	               (int)pts.size() );
	ImVec2 textSize = ImGui::CalcTextSize( hint );
	ImVec2 at( pos.x + Ui( 8 ), pos.y + Ui( 8 ) );
	draw->AddRectFilled( ImVec2( at.x - Ui( 6 ), at.y - Ui( 4 ) ), ImVec2( at.x + textSize.x + Ui( 6 ), at.y + textSize.y + Ui( 4 ) ), IM_COL32( 0, 0, 0, 200 ), Ui( 4 ) );
	draw->AddText( at, amber, hint );
	draw->PopClipRect();
	if( hovered )
		ImGui::SetMouseCursor( ImGuiMouseCursor_Hand );

	auto finish = [ & ]() {
		if( pts.size() >= 3 )
		{
			int sw, sh; // the picture shown on the Input tab: the new polygon takes from it
			app.advancedOutput.SourceSize( editor.inputSource, app.composition.width, app.composition.height, sw, sh );
			screen.slices.push_back( AdvancedOutputSetup::MakePolygonFrom( UniqueName( screen.slices, "Polygon" ), pts, editor.drawTab == OutputTab,
			                                                               (float)sw, (float)sh, (float)screen.width, (float)screen.height ) );
			screen.slices.back().source = editor.inputSource;
			editor.slice = (int)screen.slices.size() - 1;
			editor.point = -1;
		}
		editor.drawing = false;
		pts.clear();
	};
	bool clicked = hovered && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) && !ImGui::IsKeyDown( ImGuiKey_Space );
	if( clicked && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
		finish(); // its first click already placed the last point
	else if( clicked && closable )
		finish();
	else if( clicked )
	{
		bool repeat = !pts.empty() && near( pts.back() ); // the same place twice adds nothing
		if( !repeat )
			pts.push_back( mouse );
	}
	else if( ImGui::IsKeyPressed( ImGuiKey_Enter ) || ImGui::IsKeyPressed( ImGuiKey_KeypadEnter ) )
		finish();
	else if( ( ImGui::IsKeyPressed( ImGuiKey_Backspace ) || ImGui::IsKeyPressed( ImGuiKey_Delete ) ) && !pts.empty() )
		pts.pop_back();
	else if( ImGui::IsKeyPressed( ImGuiKey_Escape ) || ( hovered && ImGui::IsMouseClicked( ImGuiMouseButton_Right ) ) )
	{
		editor.drawing = false;
		pts.clear();
	}
}

void DrawCanvas( App& app, Screen& screen )
{
	ImGuiIO& io = ImGui::GetIO();
	int tab     = editor.tab;
	// The Input tab shows the picture the selected slice takes from (the composition or a canvas).
	if( editor.slice >= 0 && editor.slice < (int)screen.slices.size() && !screen.slices[ editor.slice ].mask )
		editor.inputSource = screen.slices[ editor.slice ].source;
	if( !app.advancedOutput.FindCanvas( editor.inputSource ) )
		editor.inputSource.clear();
	int sourceWidth, sourceHeight;
	app.advancedOutput.SourceSize( editor.inputSource, app.composition.width, app.composition.height, sourceWidth, sourceHeight );
	float W = tab == InputTab ? (float)sourceWidth : (float)screen.width;
	float H = tab == InputTab ? (float)sourceHeight : (float)screen.height;
	// Slices on the Input tab: those taking from the picture shown.
	auto onCanvas = [ & ]( const Slice& s ) { return tab != InputTab || ( !s.mask && s.source == editor.inputSource ); };

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
	auto rotateKnob = [ & ]( const Box& box ) {
		ImVec2 top = toScreen( box.Handle( 4 ) );
		return ImVec2( top.x, top.y - Ui( 24 ) );
	};
	// Squared screen distance from the mouse.
	auto mouseDistance2 = [ & ]( ImVec2 p ) { return ( io.MousePos.x - p.x ) * ( io.MousePos.x - p.x ) + ( io.MousePos.y - p.y ) * ( io.MousePos.y - p.y ); };

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

	GLuint texture = tab == OutputTab                ? app.outputs.ScreenTexture( (size_t)editor.screen )
	                 : editor.inputSource.empty() ? app.compositor.CompositionTexture()
	                                              : app.compositor.CanvasTexture( editor.inputSource );
	if( texture )
		draw->AddImage( TextureId( texture ), toScreen( { 0, 0 } ), toScreen( { W, H } ), ImVec2( 0, 1 ), ImVec2( 1, 0 ) );
	draw->AddRect( toScreen( { 0, 0 } ), toScreen( { W, H } ), IM_COL32( 90, 90, 90, 255 ) );

	int selected = editor.slice >= 0 && editor.slice < (int)screen.slices.size() ? editor.slice : -1;
	// A selected point belongs to one slice on one tab.
	if( editor.pointScreen != editor.screen || editor.pointSlice != selected || editor.pointTab != tab )
	{
		editor.point       = -1;
		editor.pointScreen = editor.screen;
		editor.pointSlice  = selected;
		editor.pointTab    = tab;
	}
	bool transforming = Transforming( tab );
	if( selected < 0 || transforming || editor.point >= (int)EditablePoints( screen.slices[ selected ], tab ).size() )
		editor.point = -1;

	// Slice outlines; the selected one with its handles (and its grid on the output).
	for( int i = 0; i < (int)screen.slices.size(); ++i )
	{
		const Slice& s           = screen.slices[ i ];
		std::vector< Vec2 > line = Outline( s, tab );
		if( line.size() < 2 || !onCanvas( s ) )
			continue;
		std::vector< ImVec2 > pts;
		for( Vec2 p : line )
			pts.push_back( toScreen( p ) );
		ImU32 colour = i == selected ? theme::Accent
		               : !s.enabled ? IM_COL32( 140, 140, 140, 90 )
		               : s.mask     ? IM_COL32( 255, 110, 110, 170 ) // masks in red
		                            : IM_COL32( 230, 230, 230, 150 );
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
	const ImU32 white = IM_COL32( 255, 255, 255, 255 ), black = IM_COL32( 0, 0, 0, 200 ), amber = IM_COL32( 255, 200, 80, 255 );
	const ImU32 lattice = ( theme::Accent & 0x00FFFFFF ) | 0x6E000000;
	float radius        = Ui( 4.5f );
	if( selected >= 0 )
	{
		Slice& s = screen.slices[ selected ];
		if( tab == OutputTab && !s.polygon && ( s.warp == WarpMode::Grid || s.warp == WarpMode::Curved ) )
		{
			for( int r = 0; r < s.gridRows; ++r )
			{
				std::vector< ImVec2 > row;
				for( int k = 0; k <= 32; ++k )
					row.push_back( toScreen( s.Map( k / 32.0f, (float)r / ( s.gridRows - 1 ) ) ) );
				draw->AddPolyline( row.data(), (int)row.size(), lattice, 0, Ui( 1 ) );
			}
			for( int c = 0; c < s.gridColumns; ++c )
			{
				std::vector< ImVec2 > column;
				for( int k = 0; k <= 32; ++k )
					column.push_back( toScreen( s.Map( (float)c / ( s.gridColumns - 1 ), k / 32.0f ) ) );
				draw->AddPolyline( column.data(), (int)column.size(), lattice, 0, Ui( 1 ) );
			}
		}
		else if( EditsContour( s, tab ) && !s.mask )
		{
			// How the picture spreads inside the contour: a line at every quarter of the slice.
			const int n                = 32;
			std::vector< Vec2 > mapped = s.MapGrid( n );
			for( int k = 1; k < 4; ++k )
			{
				std::vector< ImVec2 > row, column;
				for( int j = 0; j <= n; ++j )
				{
					row.push_back( toScreen( mapped[ (size_t)( k * n / 4 ) * ( n + 1 ) + j ] ) );
					column.push_back( toScreen( mapped[ (size_t)j * ( n + 1 ) + k * n / 4 ] ) );
				}
				draw->AddPolyline( row.data(), (int)row.size(), lattice, 0, Ui( 1 ) );
				draw->AddPolyline( column.data(), (int)column.size(), lattice, 0, Ui( 1 ) );
			}
		}

		if( transforming )
		{
			Box box = OutlineBox( s, tab );
			draw->AddRect( toScreen( { box.minX, box.minY } ), toScreen( { box.maxX, box.maxY } ), theme::Accent, 0, 0, Ui( 1 ) );
			ImVec2 knob = rotateKnob( box );
			draw->AddLine( toScreen( box.Handle( 4 ) ), knob, theme::Accent, Ui( 1 ) );
			bool hotKnob = editor.drag == Drag::Rotate || ( editor.drag == Drag::None && hovered && mouseDistance2( knob ) < radius * radius * 4 );
			draw->AddCircleFilled( knob, radius, hotKnob ? white : theme::Accent );
			draw->AddCircle( knob, radius, black );
			for( int k = 0; k < 8; ++k )
			{
				ImVec2 p = toScreen( box.Handle( k ) );
				bool hot = ( editor.drag == Drag::Scale && editor.handle == k ) || ( editor.drag == Drag::None && hovered && mouseDistance2( p ) < radius * radius * 4 );
				draw->AddRectFilled( ImVec2( p.x - radius, p.y - radius ), ImVec2( p.x + radius, p.y + radius ), hot ? white : theme::Accent );
				draw->AddRect( ImVec2( p.x - radius, p.y - radius ), ImVec2( p.x + radius, p.y + radius ), black );
			}
		}
		else
		{
			for( BezierHandle h : VisibleHandles( s, tab ) )
			{
				ImVec2 p = toScreen( s.contour[ h.point ].p ), q = toScreen( HandlePosition( s, h ) );
				if( ( q.x - p.x ) * ( q.x - p.x ) + ( q.y - p.y ) * ( q.y - p.y ) < radius * radius * 2 )
					continue; // too short to grab apart from its point
				bool hot = ( editor.drag == Drag::Bezier && editor.handle == h.point && editor.handleOut == h.out )
				           || ( editor.drag == Drag::None && hovered && mouseDistance2( q ) < radius * radius * 4 );
				draw->AddLine( p, q, amber, Ui( 1 ) );
				draw->AddCircleFilled( q, radius * 0.8f, hot ? white : amber );
				draw->AddCircle( q, radius * 0.8f, black );
			}
			std::vector< Vec2* > points = EditablePoints( s, tab );
			bool contour                = EditsContour( s, tab );
			for( size_t k = 0; k < points.size(); ++k )
			{
				ImVec2 p  = toScreen( *points[ k ] );
				bool hot  = ( editor.drag == Drag::Handle && editor.handle == (int)k ) || ( editor.drag == Drag::None && hovered && mouseDistance2( p ) < radius * radius * 4 );
				ImU32 fill = hot ? white : editor.point == (int)k ? amber : theme::Accent;
				if( contour && !s.IsContourCorner( (int)k ) )
				{
					// Contour points between the corners are round; corners stay square.
					draw->AddCircleFilled( p, radius, fill );
					draw->AddCircle( p, radius, black );
				}
				else
				{
					draw->AddRectFilled( ImVec2( p.x - radius, p.y - radius ), ImVec2( p.x + radius, p.y + radius ), fill );
					draw->AddRect( ImVec2( p.x - radius, p.y - radius ), ImVec2( p.x + radius, p.y + radius ), black );
				}
			}
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

	// A polygon being drawn: on another screen or tab it is given up.
	if( editor.drawing && ( editor.drawScreen != editor.screen || editor.drawTab != tab ) )
	{
		editor.drawing = false;
		editor.drawPoints.clear();
	}
	if( editor.drawing )
		return DrawPolygonTool( app, screen, draw, pos, size, toScreen, toContent, scale, W, H, hovered );
	draw->PopClipRect();

	// Picking and dragging.
	Vec2 mouse      = toContent( io.MousePos );
	float pick      = Ui( 9 ) * Ui( 9 );
	float nearLine  = Ui( 8 ) / scale; // how close to the outline a click adds a point, in content pixels
	// The editable point under the mouse, or -1.
	auto pointUnderMouse = [ & ]( Slice& s ) {
		int hit                     = -1;
		float best                  = pick;
		std::vector< Vec2* > points = EditablePoints( s, tab );
		for( size_t k = 0; k < points.size(); ++k )
		{
			float d = mouseDistance2( toScreen( *points[ k ] ) );
			if( d < best )
			{
				best = d;
				hit  = (int)k;
			}
		}
		return hit;
	};
	bool clicked = ImGui::IsItemClicked( ImGuiMouseButton_Left ) && !ImGui::IsKeyDown( ImGuiKey_Space );
	// Double-click: a contour point turns smooth or straight; the outline of a contour or polygon gets a new point,
	// which the click below then picks up for dragging.
	if( clicked && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) && selected >= 0 && !transforming )
	{
		Slice& s = screen.slices[ selected ];
		int at   = pointUnderMouse( s );
		if( at >= 0 && EditsContour( s, tab ) )
		{
			TogglePointShape( s, at );
			editor.point = at;
			clicked      = false;
		}
		else if( at < 0 )
		{
			int fresh = InsertPoint( s, tab, mouse, nearLine );
			if( fresh >= 0 )
				editor.point = fresh;
		}
	}
	if( clicked )
	{
		editor.drag   = Drag::None;
		editor.handle = -1;
		if( selected >= 0 )
		{
			Slice& s   = screen.slices[ selected ];
			float best = pick;
			if( transforming )
			{
				Box box = OutlineBox( s, tab );
				if( mouseDistance2( rotateKnob( box ) ) < best )
				{
					best        = mouseDistance2( rotateKnob( box ) );
					editor.drag = Drag::Rotate;
				}
				for( int k = 0; k < 8; ++k )
				{
					float d = mouseDistance2( toScreen( box.Handle( k ) ) );
					if( d < best )
					{
						best          = d;
						editor.drag   = Drag::Scale;
						editor.handle = k;
					}
				}
				editor.box = box;
			}
			else
			{
				for( BezierHandle h : VisibleHandles( s, tab ) )
				{
					ImVec2 p = toScreen( s.contour[ h.point ].p ), q = toScreen( HandlePosition( s, h ) );
					if( ( q.x - p.x ) * ( q.x - p.x ) + ( q.y - p.y ) * ( q.y - p.y ) < radius * radius * 2 )
						continue;
					float d = mouseDistance2( q );
					if( d < best )
					{
						best             = d;
						editor.drag      = Drag::Bezier;
						editor.handle    = h.point;
						editor.handleOut = h.out;
					}
				}
				int at = pointUnderMouse( s );
				if( at >= 0 && mouseDistance2( toScreen( *EditablePoints( s, tab )[ at ] ) ) < best )
				{
					editor.drag   = Drag::Handle;
					editor.handle = at;
				}
				if( editor.drag == Drag::Handle )
				{
					editor.point = editor.handle;
					// Alt-drag on a contour point pulls fresh Bézier handles out of it.
					if( io.KeyAlt && EditsContour( s, tab ) && s.pointMode == PointMode::Bezier )
					{
						editor.drag      = Drag::Bezier;
						editor.handleOut = true;
						editor.link      = Link::Symmetric;
					}
				}
				else if( editor.drag == Drag::Bezier )
				{
					// Handles in line stay in line; Alt moves one alone.
					const ContourPoint& c = s.contour[ editor.handle ];
					float li = std::hypot( c.in.x, c.in.y ), lo = std::hypot( c.out.x, c.out.y );
					float cross = c.in.x * c.out.y - c.in.y * c.out.x, dot = c.in.x * c.out.x + c.in.y * c.out.y;
					bool inLine = li > 0 && lo > 0 && dot < 0 && std::fabs( cross ) < 0.02f * li * lo;
					editor.link = inLine && !io.KeyAlt ? Link::Opposite : Link::None;
				}
			}
		}
		if( editor.drag == Drag::None )
		{
			int hit = -1;
			for( int i = (int)screen.slices.size() - 1; i >= 0 && hit < 0; --i )
				if( onCanvas( screen.slices[ i ] ) && PointInPolygon( Outline( screen.slices[ i ], tab ), mouse ) )
					hit = i;
			editor.slice = hit;
			editor.point = -1;
			selected     = hit;
			if( hit >= 0 )
				editor.drag = Drag::Move;
		}
		if( editor.drag != Drag::None && selected >= 0 )
		{
			editor.dragFrom = mouse;
			BeginEdit( screen.slices[ selected ], tab );
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
			else if( editor.drag == Drag::Bezier && editor.handle >= 0 && editor.handle < (int)s.contour.size() && s.contour.size() == editor.beforeContour.size() )
			{
				ContourPoint& c         = s.contour[ editor.handle ];
				const ContourPoint& was = editor.beforeContour[ editor.handle ];
				Vec2& moved             = editor.handleOut ? c.out : c.in;
				Vec2& other             = editor.handleOut ? c.in : c.out;
				if( editor.link == Link::Symmetric )
				{
					moved = { mouse.x - c.p.x, mouse.y - c.p.y };
					other = { -moved.x, -moved.y };
				}
				else
				{
					Vec2 start = editor.handleOut ? was.out : was.in;
					moved      = { start.x + delta.x, start.y + delta.y };
					float l    = std::hypot( moved.x, moved.y );
					if( editor.link == Link::Opposite && l > 1e-4f )
					{
						Vec2 kept    = editor.handleOut ? was.in : was.out;
						float length = std::hypot( kept.x, kept.y );
						other        = { -moved.x / l * length, -moved.y / l * length };
					}
				}
			}
			else if( editor.drag == Drag::Scale && editor.handle >= 0 && editor.handle < 8 )
			{
				const Box& box = editor.box;
				Vec2 grabbed   = box.Handle( editor.handle );
				Vec2 anchor    = io.KeyAlt ? box.Centre() : box.Handle( Box::Opposite( editor.handle ) );
				bool sx = editor.handle != 4 && editor.handle != 6, sy = editor.handle != 5 && editor.handle != 7;
				auto factor = []( float to, float from, float about ) {
					float span = from - about;
					return std::fabs( span ) > 1e-3f ? ( to - about ) / span : 1.0f;
				};
				float fx = sx ? factor( grabbed.x + delta.x, grabbed.x, anchor.x ) : 1;
				float fy = sy ? factor( grabbed.y + delta.y, grabbed.y, anchor.y ) : 1;
				if( io.KeyShift )
				{
					// Keep the proportions.
					if( sx && sy )
						fx = fy = std::fabs( fx ) > std::fabs( fy ) ? fx : fy;
					else if( sx )
						fy = fx;
					else
						fx = fy;
				}
				TransformPoints(
					s, tab, [ & ]( Vec2 p ) { return Vec2{ anchor.x + ( p.x - anchor.x ) * fx, anchor.y + ( p.y - anchor.y ) * fy }; },
					[ & ]( Vec2 v ) { return Vec2{ v.x * fx, v.y * fy }; } );
			}
			else if( editor.drag == Drag::Rotate )
			{
				Vec2 c      = editor.box.Centre();
				float angle = std::atan2( mouse.y - c.y, mouse.x - c.x ) - std::atan2( editor.dragFrom.y - c.y, editor.dragFrom.x - c.x );
				if( io.KeyShift )
					angle = std::round( angle / ( kPi / 12 ) ) * ( kPi / 12 );
				float cs = std::cos( angle ), sn = std::sin( angle );
				auto rotate = [ & ]( Vec2 v ) { return Vec2{ v.x * cs - v.y * sn, v.x * sn + v.y * cs }; };
				TransformPoints(
					s, tab,
					[ & ]( Vec2 p ) {
						Vec2 r = rotate( { p.x - c.x, p.y - c.y } );
						return Vec2{ c.x + r.x, c.y + r.y };
					},
					rotate );
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

	// Keys: arrows nudge the selected point, or the whole slice (Shift: 10 pixels); Delete removes the point.
	if( selected >= 0 && ( hovered || focused ) && !io.WantTextInput )
	{
		Slice& s                    = screen.slices[ selected ];
		std::vector< Vec2* > points = EditablePoints( s, tab );
		float step                  = io.KeyShift ? 10.0f : 1.0f;
		Vec2 nudge;
		if( ImGui::IsKeyPressed( ImGuiKey_LeftArrow ) )
			nudge.x -= step;
		if( ImGui::IsKeyPressed( ImGuiKey_RightArrow ) )
			nudge.x += step;
		if( ImGui::IsKeyPressed( ImGuiKey_UpArrow ) )
			nudge.y -= step;
		if( ImGui::IsKeyPressed( ImGuiKey_DownArrow ) )
			nudge.y += step;
		bool onePoint = editor.point >= 0 && editor.point < (int)points.size() && ( tab == OutputTab || s.polygon );
		if( nudge.x != 0 || nudge.y != 0 )
			for( size_t k = 0; k < points.size(); ++k )
				if( !onePoint || (int)k == editor.point )
				{
					points[ k ]->x += nudge.x;
					points[ k ]->y += nudge.y;
				}
		if( editor.point >= 0 && ( ImGui::IsKeyPressed( ImGuiKey_Delete ) || ImGui::IsKeyPressed( ImGuiKey_Backspace ) ) && RemovePoint( s, tab, editor.point ) )
			editor.point = -1;
		if( ImGui::IsKeyPressed( ImGuiKey_Escape ) )
			editor.point = -1;
	}

	if( ImGui::IsItemClicked( ImGuiMouseButton_Right ) )
	{
		editor.menuPoint     = -1;
		editor.menuAt        = mouse;
		editor.menuCanInsert = false;
		if( selected >= 0 && !transforming )
		{
			Slice& s         = screen.slices[ selected ];
			editor.menuPoint = pointUnderMouse( s );
			if( editor.menuPoint >= 0 )
				editor.point = editor.menuPoint;
			else if( HasPointEditing( s, tab ) )
			{
				Slice probe          = s;
				editor.menuCanInsert = InsertPoint( probe, tab, mouse, nearLine * 2 ) >= 0;
			}
		}
		if( editor.menuPoint < 0 && !editor.menuCanInsert )
			for( int i = (int)screen.slices.size() - 1; i >= 0; --i )
				if( onCanvas( screen.slices[ i ] ) && PointInPolygon( Outline( screen.slices[ i ], tab ), mouse ) )
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
			Slice& s     = screen.slices[ editor.slice ];
			bool contour = EditsContour( s, tab );
			if( editor.menuPoint >= 0 && editor.menuPoint < (int)EditablePoints( s, tab ).size() )
			{
				ImGui::TextDisabled( "%s - point %d", s.name.c_str(), editor.menuPoint + 1 );
				if( contour && s.pointMode == PointMode::Bezier )
				{
					if( ImGui::MenuItem( "Smooth Point" ) )
						s.SmoothContourPoint( editor.menuPoint );
					if( ImGui::MenuItem( "Straight Point" ) )
						s.contour[ editor.menuPoint ] = Straightened( s, editor.menuPoint );
				}
				bool removable = s.polygon ? s.input.size() > 3 : s.mask ? s.contour.size() > 3 : contour && !s.IsContourCorner( editor.menuPoint );
				if( HasPointEditing( s, tab ) && ImGui::MenuItem( "Delete Point", "Del", false, removable ) && RemovePoint( s, tab, editor.menuPoint ) )
				{
					editor.point     = -1;
					editor.menuPoint = -1;
				}
				ImGui::Separator();
			}
			else
				ImGui::TextDisabled( "%s", s.name.c_str() );
			if( editor.menuCanInsert && ImGui::MenuItem( "Add Point Here" ) )
			{
				int fresh = InsertPoint( s, tab, editor.menuAt, nearLine * 2 );
				if( fresh >= 0 )
					editor.point = fresh;
			}
			if( contour && !s.mask && ImGui::MenuItem( "Reset Contour" ) )
			{
				s.ResetContour();
				editor.point = -1;
			}
			if( s.mask )
			{
				ImGui::MenuItem( "Invert", nullptr, &s.invert );
				if( ImGui::MenuItem( "Fit to Screen" ) )
					s.FitOutput( (float)screen.width, (float)screen.height );
				ImGui::MenuItem( "Enabled", nullptr, &s.enabled );
				ImGui::EndPopup();
				return;
			}
			if( ImGui::MenuItem( "Match Output to Input" ) )
				s.MatchOutputToInput( (float)app.composition.width, (float)app.composition.height, (float)screen.width, (float)screen.height );
			if( ImGui::MenuItem( "Fit Output to Screen" ) )
				s.FitOutput( (float)screen.width, (float)screen.height );
			if( !s.polygon && ImGui::MenuItem( s.source.empty() ? "Input: Whole Composition" : "Input: Whole Canvas" ) )
			{
				int w, h;
				app.advancedOutput.SourceSize( s.source, app.composition.width, app.composition.height, w, h );
				SetRect( s, 0, 0, (float)w, (float)h );
			}
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
	std::string current = screen.device == OutputDevice::Display ? OutputWindow::DisplayLabel( screen.display ) : OutputDeviceName( screen.device );
	if( ImGui::BeginCombo( "##device", current.c_str() ) )
	{
		if( ImGui::Selectable( "Virtual Output", screen.device == OutputDevice::Virtual ) )
			screen.device = OutputDevice::Virtual;
		if( ImGui::Selectable( "Window", screen.device == OutputDevice::Window ) )
			screen.device = OutputDevice::Window;
		for( const OutputWindow::Display& d : OutputWindow::Displays() )
		{
			std::string label = d.name + "  " + std::to_string( d.width ) + "x" + std::to_string( d.height );
			if( ImGui::Selectable( label.c_str(), screen.device == OutputDevice::Display && OutputWindow::FindDisplay( screen.display ) == d.monitor ) )
			{
				screen.device  = OutputDevice::Display;
				screen.display = d.id;
			}
		}
		ImGui::EndCombo();
	}
	Row( "" );
	if( screen.device == OutputDevice::Virtual )
		ImGui::TextWrapped( "Virtual Output: drawn here only. Choose a display or Window above to show it." );
	else
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
				if( d.monitor == OutputWindow::FindDisplay( screen.display ) )
				{
					screen.width  = d.width;
					screen.height = d.height;
				}
	}
	Row( "Enabled" );
	ImGui::Checkbox( "##enabled", &screen.enabled );
	Row( "Test Pattern" );
	ImGui::Checkbox( "##test", &screen.testPattern );
	if( const char* local = LocalSharingName() )
	{
		Row( local );
		ImGui::Checkbox( "##share", &screen.share );
		if( ImGui::IsItemHovered() )
			ImGui::SetTooltip( "Send this screen to other programs on this computer as \"STLT-VJ %s\"", screen.name.c_str() );
	}
	Row( "NDI" );
	ImGui::Checkbox( "##ndi", &screen.ndi );
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "Send this screen over the network as the NDI source \"STLT-VJ %s\"", screen.name.c_str() );
	std::string sharing = app.outputs.SharingStatus( (size_t)editor.screen );
	if( !sharing.empty() )
	{
		Row( "" );
		ImGui::PushTextWrapPos( 0 );
		ImGui::TextDisabled( "%s", sharing.c_str() );
		ImGui::PopTextWrapPos();
	}
	Row( "Opacity" );
	SliderPercent( "##opacity", &screen.opacity, 0, 1, 1 );
	Row( "Brightness" );
	FilledSlider( "##brightness", &screen.brightness, -1, 1, "%.2f" );
	ResetValue( &screen.brightness, 0 );
	Row( "Contrast" );
	FilledSlider( "##contrast", &screen.contrast, -1, 1, "%.2f" );
	ResetValue( &screen.contrast, 0 );
	Row( "Red" );
	FilledSlider( "##red", &screen.red, -1, 1, "%.2f" );
	ResetValue( &screen.red, 0 );
	Row( "Green" );
	FilledSlider( "##green", &screen.green, -1, 1, "%.2f" );
	ResetValue( &screen.green, 0 );
	Row( "Blue" );
	FilledSlider( "##blue", &screen.blue, -1, 1, "%.2f" );
	ResetValue( &screen.blue, 0 );
	ImGui::EndTable();
}

// The output mode and the selected point: shared by slices and masks.
void EditModeRows( Slice& s )
{
	if( editor.tab == OutputTab )
	{
		Row( "Mode" );
		if( ImGui::RadioButton( "Edit Points", editor.outputEdit == OutputEdit::Points ) )
			editor.outputEdit = OutputEdit::Points;
		ImGui::SameLine();
		if( ImGui::RadioButton( "Transform", editor.outputEdit == OutputEdit::Transform ) )
			editor.outputEdit = OutputEdit::Transform;
	}
	std::vector< Vec2* > points = EditablePoints( s, editor.tab );
	if( editor.point >= 0 && editor.point < (int)points.size() )
	{
		Row( "Point" );
		ImGui::DragFloat2( "##point", &points[ editor.point ]->x, 1, -16384, 16384, "%.1f" );
	}
}

void PointModeRows( Slice& s )
{
	Row( "Point Mode" );
	if( ImGui::BeginCombo( "##pointMode", PointModeName( s.pointMode ) ) )
	{
		for( int m = 0; m < (int)PointMode::Count; ++m )
			if( ImGui::Selectable( PointModeName( (PointMode)m ), (int)s.pointMode == m ) )
				s.pointMode = (PointMode)m;
		ImGui::EndCombo();
	}
	Row( "Points" );
	ImGui::Text( "%d", (int)s.contour.size() );
}

void DrawMaskProps( Screen& screen, Slice& s )
{
	ImGui::SeparatorText( "Mask" );
	if( !BeginProps( "##mask" ) )
		return;
	Row( "Name" );
	ImGui::InputText( "##name", &s.name );
	Row( "Enabled" );
	ImGui::Checkbox( "##enabled", &s.enabled );
	Row( "Strength" );
	SliderPercent( "##opacity", &s.opacity, 0, 1, 1 );
	Row( "Invert" );
	ImGui::Checkbox( "##invert", &s.invert );
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "Black outside the shape instead of inside" );
	Row( "Feather" );
	FilledSlider( "##feather", &s.feather, 0, 200, "%.0f px", ImGuiSliderFlags_AlwaysClamp );
	ResetValue( &s.feather, 0 );
	EditModeRows( s );
	PointModeRows( s );
	Row( "" );
	if( ImGui::Button( "Smooth All" ) )
		for( int i = 0; i < (int)s.contour.size(); ++i )
			s.SmoothContourPoint( i );
	ImGui::SameLine();
	if( ImGui::Button( "Fit Screen" ) )
		s.FitOutput( (float)screen.width, (float)screen.height );
	ImGui::EndTable();
	ImGui::TextDisabled( "Blacks out what is below it in the list." );
}

void DrawSliceProps( App& app, Screen& screen, Slice& s )
{
	if( s.mask )
	{
		DrawMaskProps( screen, s );
		return;
	}
	AdvancedOutputSetup& setup = app.advancedOutput;
	Composition& comp          = app.composition;
	int sourceW, sourceH;
	setup.SourceSize( s.source, comp.width, comp.height, sourceW, sourceH );
	float cw = (float)sourceW, ch = (float)sourceH; // the picture the slice takes from
	ImGui::SeparatorText( s.polygon ? "Polygon" : "Slice" );
	if( !BeginProps( "##slice" ) )
		return;
	Row( "Name" );
	ImGui::InputText( "##name", &s.name );

	// Where its picture comes from: the composition, or a canvas the timeline's tracks can be sent to.
	Row( "Source" );
	{
		auto label = [ & ]( const std::string& id ) {
			const Canvas* c = setup.FindCanvas( id );
			char text[ 160 ];
			std::snprintf( text, sizeof( text ), "%s  %dx%d", c ? c->name.c_str() : "Composition", c ? c->width : comp.width, c ? c->height : comp.height );
			return std::string( text );
		};
		ImGui::SetNextItemWidth( -FLT_MIN );
		if( ImGui::BeginCombo( "##source", label( s.source ).c_str() ) )
		{
			if( ImGui::Selectable( label( "" ).c_str(), s.source.empty() ) )
				setup.SetSliceSource( s, "", comp.width, comp.height );
			for( const Canvas& c : setup.canvases )
				if( ImGui::Selectable( ( label( c.id ) + "##" + c.id ).c_str(), s.source == c.id ) )
					setup.SetSliceSource( s, c.id, comp.width, comp.height );
			ImGui::Separator();
			if( ImGui::Selectable( "New Canvas" ) )
			{
				setup.canvases.push_back( setup.MakeCanvas( screen.name, screen.width, screen.height ) );
				setup.SetSliceSource( s, setup.canvases.back().id, comp.width, comp.height );
			}
			if( ImGui::IsItemHovered() )
				ImGui::SetTooltip( "A picture of its own, the size of this screen: send timeline tracks to it (the chip next to a track's name)" );
			ImGui::EndCombo();
		}
		if( ImGui::IsItemHovered() )
			ImGui::SetTooltip( "What this slice shows a part of: the composition, or a canvas that timeline tracks are sent to" );
		if( Canvas* c = const_cast< Canvas* >( setup.FindCanvas( s.source ) ) )
		{
			// The canvas itself, shared by every slice taking from it.
			Row( "Canvas" );
			ImGui::SetNextItemWidth( -FLT_MIN );
			ImGui::InputText( "##canvasName", &c->name );
			Row( "Canvas W, H" );
			int size[ 2 ] = { c->width, c->height };
			ImGui::SetNextItemWidth( -FLT_MIN );
			if( ImGui::DragInt2( "##canvasSize", size, 1, 16, 16384 ) )
			{
				// The slices on it keep their share of it.
				float sx = (float)size[ 0 ] / c->width, sy = (float)size[ 1 ] / c->height;
				for( Screen& sc : setup.screens )
					for( Slice& other : sc.slices )
						if( other.source == c->id )
							for( Vec2& p : other.input )
								p = { p.x * sx, p.y * sy };
				c->width  = std::clamp( size[ 0 ], 16, 16384 );
				c->height = std::clamp( size[ 1 ], 16, 16384 );
			}
			std::string fed;
			for( const TimelineTrack& t : comp.timeline.tracks )
				if( t.output == c->id && !t.audio )
					fed += ( fed.empty() ? "" : ", " ) + t.name;
			Row( "Tracks" );
			ImGui::TextDisabled( "%s", fed.empty() ? "none yet: choose this canvas on a timeline track" : fed.c_str() );
			Row( "" );
			if( ImGui::Button( "Remove Canvas" ) )
			{
				std::string id = c->id;
				setup.RemoveCanvas( id, comp.width, comp.height );
				for( TimelineTrack& t : comp.timeline.tracks )
					if( t.output == id )
						t.output.clear();
			}
			if( ImGui::IsItemHovered() )
				ImGui::SetTooltip( "Its slices go back to the composition, and its tracks too" );
		}
	}
	Row( "Enabled" );
	ImGui::Checkbox( "##enabled", &s.enabled );
	Row( "Opacity" );
	SliderPercent( "##opacity", &s.opacity, 0, 1, 1 );
	if( s.polygon )
	{
		Row( "Points" );
		ImGui::Text( "%d", (int)s.input.size() );
		ImGui::SameLine();
		ImGui::TextDisabled( "(?)" );
		if( ImGui::IsItemHovered() )
			ImGui::SetTooltip( "Add a point: double-click the outline, or right-click it > Add Point Here (Input or Output tab).\n"
			                   "Remove one: select it and press Del, or right-click it > Delete Point.\n"
			                   "A new shape from scratch: + Polygon > Draw." );
	}

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
		if( ImGui::Button( s.source.empty() ? "Whole Composition" : "Whole Canvas" ) )
			SetRect( s, 0, 0, cw, ch );
	}
	EditModeRows( s );
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
		if( s.warp == WarpMode::Contour )
		{
			PointModeRows( s );
			Row( "" );
			if( ImGui::Button( "Smooth All" ) )
				for( int i = 0; i < (int)s.contour.size(); ++i )
					s.SmoothContourPoint( i );
			ImGui::SameLine();
			if( ImGui::Button( "Reset Contour" ) )
			{
				s.ResetContour();
				editor.point = -1;
			}
		}
		else if( s.warp != WarpMode::Perspective )
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
		SliderPercent( "##left", &s.edge.left, 0, 0.5f, 0 );
		Row( "Right" );
		SliderPercent( "##right", &s.edge.right, 0, 0.5f, 0 );
		Row( "Top" );
		SliderPercent( "##top", &s.edge.top, 0, 0.5f, 0 );
		Row( "Bottom" );
		SliderPercent( "##bottom", &s.edge.bottom, 0, 0.5f, 0 );
		Row( "Power" );
		FilledSlider( "##power", &s.edge.power, 0.5f, 6, "%.2f" );
		ResetValue( &s.edge.power, SoftEdge{}.power );
		Row( "Luminance" );
		FilledSlider( "##luminance", &s.edge.luminance, 0, 1, "%.2f" );
		ResetValue( &s.edge.luminance, SoftEdge{}.luminance );
		Row( "Gamma" );
		FilledSlider( "##gamma", &s.edge.gamma, 0.5f, 3, "%.2f" );
		ResetValue( &s.edge.gamma, SoftEdge{}.gamma );
		ImGui::EndTable();
	}
}
}

ImU32 PictureColour( const App& app, const std::string& source )
{
	static const ImU32 palette[] = { IM_COL32( 245, 184, 74, 255 ), IM_COL32( 230, 130, 175, 255 ), IM_COL32( 120, 205, 140, 255 ),
		                             IM_COL32( 180, 150, 240, 255 ), IM_COL32( 100, 205, 210, 255 ), IM_COL32( 235, 120, 95, 255 ) };
	const std::vector< Canvas >& canvases = app.advancedOutput.canvases;
	for( size_t i = 0; i < canvases.size(); ++i )
		if( canvases[ i ].id == source )
			return palette[ i % ( sizeof( palette ) / sizeof( palette[ 0 ] ) ) ];
	return theme::Accent;
}

std::string PictureName( const App& app, const std::string& source )
{
	const Canvas* c = app.advancedOutput.FindCanvas( source );
	return c ? c->name : "Composition";
}

void ShowAdvancedOutputTab( int tab, int screen, int slice )
{
	editor.tab    = tab == 1 ? OutputTab : InputTab;
	editor.screen = screen;
	editor.slice  = slice;
}

void DrawAdvancedOutputSavePrompt( App& app )
{
	// The editor closed (View menu, Ctrl+Shift+A) with unsaved edits: it stays open and asks, at its top.
	if( app.advancedEditorWasOpen && !app.panels.advancedOutput && app.outputSavePrompt == 0 && app.OutputUnsaved() )
	{
		app.panels.advancedOutput = true;
		app.outputSavePrompt      = 1;
	}
	if( app.outputSavePrompt != 0 && !app.advancedEditorWasOpen )
		ImGui::SetWindowFocus( panel::AdvancedOutput );
	app.advancedEditorWasOpen = app.panels.advancedOutput;
}

namespace
{
// The question when the editor closes, or the app quits, with unsaved edits: save, discard, or go on editing.
void DrawSaveQuestion( App& app )
{
	bool quitting = app.outputSavePrompt == 2;
	ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4( 0.55f, 0.16f, 0.14f, 0.85f ) );
	ImGui::BeginChild( "##saveQuestion", ImVec2( 0, ImGui::GetFrameHeightWithSpacing() + Ui( 10 ) ), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar );
	ImGui::SetCursorPos( ImVec2( Ui( 8 ), Ui( 5 ) ) );
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted( quitting ? "Quitting: save the changes to the Advanced Output?" : "Closing: save the changes to the Advanced Output?" );
	ImGui::SameLine();
	int choice = 0;
	if( ImGui::Button( "Save" ) )
		choice = 1;
	ImGui::SameLine();
	if( ImGui::Button( "Discard" ) )
		choice = 2;
	ImGui::SameLine();
	if( ImGui::Button( quitting ? "Don't Quit" : "Keep Editing" ) || ImGui::IsKeyPressed( ImGuiKey_Escape, false ) )
		choice = 3;
	ImGui::EndChild();
	ImGui::PopStyleColor();
	if( choice == 0 )
		return;
	if( choice == 1 )
		app.SaveOutputSetup();
	else if( choice == 2 )
		app.RevertOutputSetup();
	if( choice == 3 )
	{
		app.outputSavePrompt = 0;
		return;
	}
	app.panels.advancedOutput = false;
	app.advancedEditorWasOpen = false;
	// When quitting the question stays answered (2), so the app may close.
	if( quitting )
		app.quitRequested = true;
	else
		app.outputSavePrompt = 0;
}
}

void DrawAdvancedOutputPanel( App& app )
{
	ImGui::SetNextWindowSize( ImVec2( Ui( 1290 ), Ui( 740 ) ), ImGuiCond_FirstUseEver );
	bool shown = ImGui::Begin( panel::AdvancedOutput, &app.panels.advancedOutput );
	// Closed with unsaved edits: it stays, and asks (DrawAdvancedOutputSavePrompt).
	if( !app.panels.advancedOutput && app.outputSavePrompt == 0 && app.OutputUnsaved() )
	{
		app.panels.advancedOutput = true;
		app.outputSavePrompt      = 1;
	}
	if( !shown )
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

	if( app.outputSavePrompt != 0 )
		DrawSaveQuestion( app );
	// Save or discard: the edits show live, but are kept only when saved.
	{
		bool unsaved = app.OutputUnsaved();
		ImGui::BeginDisabled( !unsaved );
		if( unsaved )
			ImGui::PushStyleColor( ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4( theme::AccentDim ) );
		if( ImGui::Button( "Save" ) )
			app.SaveOutputSetup();
		if( unsaved )
			ImGui::PopStyleColor();
		ImGui::SameLine();
		if( ImGui::Button( "Discard Changes" ) )
			app.RevertOutputSetup();
		ImGui::EndDisabled();
		if( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
			ImGui::SetTooltip( "Back to the setup as it was last saved" );
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		if( unsaved )
			ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( theme::Warning ), "Unsaved changes: they show now, but are lost unless saved" );
		else
			ImGui::TextDisabled( "Saved" );
	}

	// Banners: the setup is not on show, or nothing of it reaches a screen.
	auto banner = [ & ]( const char* id, const std::string& text, const char* button ) {
		ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4( 0.45f, 0.32f, 0.10f, 0.55f ) );
		ImGui::BeginChild( id, ImVec2( 0, ImGui::GetFrameHeightWithSpacing() + Ui( 8 ) ), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar );
		ImGui::SetCursorPos( ImVec2( Ui( 8 ), Ui( 4 ) ) );
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted( text.c_str() );
		bool pressed = false;
		if( button )
		{
			ImGui::SameLine();
			pressed = ImGui::Button( button );
		}
		ImGui::EndChild();
		ImGui::PopStyleColor();
		return pressed;
	};
	bool anyDevice = std::any_of( setup.screens.begin(), setup.screens.end(), []( const Screen& s ) { return s.device != OutputDevice::Virtual && s.enabled; } );
	if( app.outputMode != OutputMode::Advanced )
	{
		if( banner( "##notLive", "This setup is not on show (Output: " + app.OutputModeLabel() + ").", "Use Advanced Output" ) )
			app.SetOutputMode( OutputMode::Advanced );
	}
	else if( !anyDevice )
	{
		if( banner( "##virtual",
		            "Nothing is shown: every screen is a Virtual Output. Select a screen in the list, then choose its Device on the right (a display or Window).",
		            "Select Screen 1" ) )
		{
			editor.screen = 0;
			editor.slice  = -1;
		}
	}

	if( ImGui::BeginTable( "##ao", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, ImGui::GetContentRegionAvail() ) )
	{
		ImGui::TableSetupColumn( "list", ImGuiTableColumnFlags_WidthFixed, Ui( 230 ) );
		ImGui::TableSetupColumn( "canvas", ImGuiTableColumnFlags_WidthStretch );
		ImGui::TableSetupColumn( "props", ImGuiTableColumnFlags_WidthFixed, Ui( 340 ) );
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
		if( editor.tab == InputTab )
		{
			const Canvas* c = app.advancedOutput.FindCanvas( editor.inputSource );
			ImGui::TextDisabled( "%s %dx%d", c ? ( "canvas " + c->name ).c_str() : "composition", c ? c->width : app.composition.width,
			                     c ? c->height : app.composition.height );
		}
		else
			ImGui::TextDisabled( "%s - screen %dx%d", screen.name.c_str(), screen.width, screen.height );
		DrawCanvas( app, screen );
		ImGui::EndChild();

		ImGui::TableSetColumnIndex( 2 );
		ImGui::BeginChild( "##right" );
		if( editor.slice >= 0 )
			DrawSliceProps( app, screen, screen.slices[ editor.slice ] );
		DrawScreenProps( app, screen );
		ImGui::Spacing();
		ImGui::TextDisabled( "Drag a slice to move it, its points to shape it.\n"
		                     "Double-click the outline of a contour or\npolygon to add a point; Del removes it.\n"
		                     "Double-click a contour point: smooth or\nstraight. Alt-drag it: new Bezier handles.\n"
		                     "Alt on a handle: move it alone.\n"
		                     "Transform: drag the box to scale (Alt: from\nthe centre), the knob to rotate.\n"
		                     "Shift: one axis, keep proportions, 15 degrees.\nAlt: free input corner. Ctrl: no snapping.\n"
		                     "Arrows: nudge the slice or the selected point.\nWheel: zoom. Middle button or Space: pan.\nRight-click a slice for more." );
		ImGui::EndChild();
		ImGui::EndTable();
	}
	ImGui::End();
}
}
