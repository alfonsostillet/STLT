#include "ui/AdvancedOutputUi.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include "App.h"
#include "Settings.h"
#include "ui/FileDialogs.h"
#include "ui/Panels.h"
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
	Pick,   // a box dragged on an empty part of the canvas: the slices it touches are picked
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

	// Several slices picked together (Ctrl or Shift + click, or a box dragged around them): they move together.
	// `slice` is the one the inspector shows, always among them.
	std::vector< int > picked;
	// A screen about to go fullscreen on the display STLT's own window is on: asked first.
	int ownScreen = -1;
	std::string ownDisplay;
	bool askOwn = false;

	int pickedScreen   = -1; // the screen, and its number of slices, the picked indices belong to
	size_t pickedCount = 0;
	Vec2 pickFrom;
	std::vector< std::pair< int, std::vector< Vec2 > > > beforeMany; // the picked slices' points when a move started

	Drag drag      = Drag::None;
	int dragTab    = InputTab; // the canvas the drag started on: side by side, only it follows the mouse
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

	// The editor's three steps: 1 Screens (where the picture goes out), 2 Stage (the stage in real size: next),
	// 3 Map (which part of the picture goes where).
	enum class Step
	{
		Screens,
		Stage,
		Map
	};
	Step step = Step::Screens;

	// The middle: Input and Output side by side, or one of them (editor.tab) large.
	enum class View
	{
		Both,
		Input,
		Output
	};
	View view = View::Both;

	// What the Input tab shows: the selected slice's picture, "" the composition or a canvas's id.
	std::string inputSource;

	// Drawing a new polygon point by point (+ Polygon > Draw): on this screen and tab, the points so far.
	bool drawing   = false;
	int drawScreen = -1, drawTab = InputTab;
	std::vector< Vec2 > drawPoints;
};

Editor editor;

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


// ---- shared look

// The colour that tells slices apart: the same in the list, on both canvases and in the inspector.
ImU32 SliceColour( int index )
{
	static const ImU32 palette[] = { IM_COL32( 255, 166, 43, 255 ), IM_COL32( 92, 200, 242, 255 ), IM_COL32( 180, 140, 255, 255 ),
		                             IM_COL32( 111, 208, 140, 255 ), IM_COL32( 255, 111, 145, 255 ), IM_COL32( 240, 220, 90, 255 ),
		                             IM_COL32( 90, 220, 200, 255 ), IM_COL32( 235, 130, 90, 255 ) };
	return palette[ std::max( index, 0 ) % 8 ];
}

// A slice's or a screen's number on a square of its colour.
float Badge( ImDrawList* draw, ImVec2 at, const char* text, ImU32 colour, float height )
{
	return TextBadge( draw, at, text, colour, IM_COL32( 17, 17, 17, 255 ), height );
}

// The title of a part of the inspector, on a bar: bold, with a faint hint after it.
void SectionBar( const char* title, const char* hint = nullptr )
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	ImVec2 p         = ImGui::GetCursorScreenPos();
	float w = ImGui::GetContentRegionAvail().x, h = Ui( 26 );
	draw->AddRectFilled( p, ImVec2( p.x + w, p.y + h ), theme::Raised );
	ImGui::PushFont( Fonts().bold );
	ImVec2 t = ImGui::CalcTextSize( title );
	draw->AddText( ImVec2( p.x + Ui( 8 ), p.y + ( h - t.y ) / 2 ), theme::Text, title );
	ImGui::PopFont();
	if( hint )
	{
		draw->PushClipRect( p, ImVec2( p.x + w, p.y + h ), true );
		draw->AddText( ImVec2( p.x + Ui( 14 ) + t.x, p.y + ( h - t.y ) / 2 ), theme::TextFaint, hint );
		draw->PopClipRect();
	}
	ImGui::Dummy( ImVec2( w, h + Ui( 2 ) ) );
}

// A faint line of explanation in the value column of a props table.
void HelpRow( const char* text )
{
	Row( "" );
	ImGui::PushStyleColor( ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4( theme::TextFaint ) );
	ImGui::PushTextWrapPos( 0 );
	ImGui::TextUnformatted( text );
	ImGui::PopTextWrapPos();
	ImGui::PopStyleColor();
}

// Where a screen goes, for people.
std::string Destination( const Screen& s )
{
	if( s.device == OutputDevice::Display )
		return OutputWindow::DisplayLabel( s.display );
	if( s.device == OutputDevice::Window )
		return "A window";
	const char* local = LocalSharingName();
	if( s.ndi )
		return "NDI only";
	if( s.share && local )
		return std::string( local ) + " only";
	return "Nowhere";
}

// Whether a screen's picture leaves the computer now: this setup on show, the screen on, and somewhere to go.
bool ScreenOnShow( const App& app, const Screen& s )
{
	return app.EditingLive() && app.outputMode == OutputMode::Advanced && s.enabled && ( s.device != OutputDevice::Virtual || s.ndi || s.share );
}

bool IsPicked( int index )
{
	return std::find( editor.picked.begin(), editor.picked.end(), index ) != editor.picked.end();
}

// Keeps the picked slices valid and with the inspector's one among them (other code may change editor.slice).
void SyncPicked( const Screen& screen )
{
	// Another screen, or slices added or deleted: the indices no longer mean the same slices.
	if( editor.pickedScreen != editor.screen || editor.pickedCount != screen.slices.size() )
	{
		editor.picked.clear();
		editor.pickedScreen = editor.screen;
		editor.pickedCount  = screen.slices.size();
	}
	editor.picked.erase( std::remove_if( editor.picked.begin(), editor.picked.end(), [ & ]( int i ) { return i < 0 || i >= (int)screen.slices.size(); } ),
	                     editor.picked.end() );
	if( editor.slice < 0 )
		editor.picked.clear();
	else if( !IsPicked( editor.slice ) )
		editor.picked = { editor.slice };
}

// Whether STLT's own window is on a display (its middle is).
bool StltOnDisplay( const App& app, const OutputWindow::Display& d )
{
	if( !app.window )
		return false;
	int wx = 0, wy = 0, ww = 0, wh = 0;
	glfwGetWindowPos( app.window, &wx, &wy );
	glfwGetWindowSize( app.window, &ww, &wh );
	return wx + ww / 2 >= d.x && wx + ww / 2 < d.x + d.width && wy + wh / 2 >= d.y && wy + wh / 2 < d.y + d.height;
}

// Sends a screen fullscreen to a display; to STLT's own display only once asked (it would cover STLT).
void SendScreenTo( const App& app, Screen& screen, int si, const OutputWindow::Display& d )
{
	if( screen.device == OutputDevice::Display && OutputWindow::FindDisplay( screen.display ) == d.monitor )
		return;
	if( StltOnDisplay( app, d ) )
	{
		editor.ownScreen  = si;
		editor.ownDisplay = d.id;
		editor.askOwn     = true;
		return;
	}
	screen.device  = OutputDevice::Display;
	screen.display = d.id;
}

void DrawOwnDisplayQuestion( AdvancedOutputSetup& setup )
{
	if( editor.askOwn )
	{
		ImGui::OpenPopup( "STLT is on that display" );
		editor.askOwn = false;
	}
	ImVec2 at = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
	ImGui::SetNextWindowPos( ImVec2( at.x + size.x / 2, at.y + size.y / 2 ), ImGuiCond_Appearing, ImVec2( 0.5f, 0.5f ) );
	ImGui::SetNextWindowSize( ImVec2( Ui( 420 ), 0 ) );
	if( !ImGui::BeginPopupModal( "STLT is on that display", nullptr, ImGuiWindowFlags_NoResize ) )
		return;
	bool valid = editor.ownScreen >= 0 && editor.ownScreen < (int)setup.screens.size();
	ImGui::TextWrapped( "%s would play fullscreen over STLT's own window, and you could not see or use STLT any more.",
	                    valid ? setup.screens[ editor.ownScreen ].name.c_str() : "The screen" );
	ImGui::Spacing();
	ImGui::TextDisabled( "Pick another display, or first move STLT's window to another one." );
	ImGui::Spacing();
	if( ImGui::Button( "Cancel" ) || ImGui::IsKeyPressed( ImGuiKey_Escape ) )
	{
		editor.ownScreen = -1;
		ImGui::CloseCurrentPopup();
	}
	ImGui::SameLine();
	if( ImGui::Button( "Send it there anyway" ) )
	{
		if( valid )
		{
			setup.screens[ editor.ownScreen ].device  = OutputDevice::Display;
			setup.screens[ editor.ownScreen ].display = editor.ownDisplay;
		}
		editor.ownScreen = -1;
		ImGui::CloseCurrentPopup();
	}
	ImGui::EndPopup();
}

// Shows another screen: what was selected belongs to the one before.
void ShowScreen( const AdvancedOutputSetup& setup, int si )
{
	if( si < 0 || si >= (int)setup.screens.size() || si == editor.screen )
		return;
	editor.screen = si;
	editor.slice  = setup.screens[ si ].slices.empty() ? -1 : 0;
	editor.point  = -1;
	editor.picked.clear();
}

// ---- left: screens and slices

void DrawList( App& app )
{
	AdvancedOutputSetup& setup = app.EditedOutput();
	Composition& comp          = app.composition;
	float cw = (float)comp.width, ch = (float)comp.height;

	// Which screen is mapped.
	{
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted( "Screen" );
		ImGui::SameLine();
		ImGui::SetNextItemWidth( -FLT_MIN );
		const Screen& current = setup.screens[ editor.screen ];
		std::string label     = std::to_string( editor.screen + 1 ) + "  " + current.name;
		if( ImGui::BeginCombo( "##mapScreen", label.c_str(), ImGuiComboFlags_HeightLarge ) )
		{
			for( int si = 0; si < (int)setup.screens.size(); ++si )
				if( ImGui::Selectable( ( std::to_string( si + 1 ) + "  " + setup.screens[ si ].name + "##" + std::to_string( si ) ).c_str(), si == editor.screen ) )
					ShowScreen( setup, si );
			ImGui::EndCombo();
		}
	}
	bool haveScreen = editor.screen >= 0 && editor.screen < (int)setup.screens.size();
	ImGui::Spacing();

	float tools = ImGui::GetFrameHeightWithSpacing() * 2 + Ui( 10 );
	ImGui::BeginChild( "##slices", ImVec2( 0, -tools ) );
	if( haveScreen )
	{
		Screen& screen = setup.screens[ editor.screen ];
		SyncPicked( screen );
		ImGui::TextDisabled( "%d item%s, the top one drawn over the others", (int)screen.slices.size(), screen.slices.size() == 1 ? "" : "s" );
		// Top of the list is drawn on top: show the slices last to first.
		for( int k = (int)screen.slices.size() - 1; k >= 0; --k )
		{
			Slice& slice = screen.slices[ k ];
			ImGui::PushID( k );
			ImGui::Checkbox( "##on", &slice.enabled );
			if( ImGui::IsItemHovered() )
				ImGui::SetTooltip( slice.enabled ? "On: untick to hide it" : "Off: tick to show it" );
			ImGui::SameLine();
			ImVec2 rowAt = ImGui::GetCursorScreenPos();
			float h      = ImGui::GetFrameHeight();
			std::string number = slice.mask ? "M" : std::to_string( k + 1 );
			float bw = Badge( ImGui::GetWindowDrawList(), ImVec2( rowAt.x, rowAt.y + Ui( 3 ) ), number.c_str(),
			                  slice.mask ? IM_COL32( 150, 150, 150, 255 ) : slice.enabled ? SliceColour( k ) : IM_COL32( 110, 110, 110, 255 ), h - Ui( 6 ) );
			ImGui::SetCursorScreenPos( ImVec2( rowAt.x + bw + Ui( 6 ), rowAt.y ) );
			ImGui::AlignTextToFramePadding();
			std::string name = slice.name + ( slice.polygon ? "  (polygon)" : "" );
			if( ImGui::Selectable( name.c_str(), IsPicked( k ), 0, ImVec2( 0, h ) ) )
			{
				// Ctrl or Shift + click: in or out of the picked slices, to move them together.
				const ImGuiIO& io = ImGui::GetIO();
				if( ( io.KeyCtrl || io.KeyShift ) && editor.slice >= 0 )
				{
					if( IsPicked( k ) && editor.picked.size() > 1 )
					{
						editor.picked.erase( std::find( editor.picked.begin(), editor.picked.end(), k ) );
						editor.slice = editor.picked.back();
					}
					else
					{
						if( !IsPicked( k ) )
							editor.picked.push_back( k );
						editor.slice = k;
					}
				}
				else
				{
					editor.slice  = k;
					editor.picked = { k };
				}
			}
			if( ImGui::IsItemClicked( ImGuiMouseButton_Right ) )
			{
				if( !IsPicked( k ) )
					editor.picked = { k };
				editor.slice = k;
				ImGui::OpenPopup( "##itemMenu" );
			}
			if( ImGui::IsItemHovered( ImGuiHoveredFlags_DelayNormal ) )
				ImGui::SetTooltip( slice.mask ? "A mask: blacks out what is below it in the list\nCtrl-click: pick several. Right-click: up, down, duplicate, delete"
				                              : "Takes from: %s\nCtrl-click: pick several, to move them together. Right-click: up, down, duplicate, delete",
				                   PictureName( app, slice.source ).c_str() );
			if( ImGui::BeginPopup( "##itemMenu" ) )
			{
				std::vector< Slice >& slices = screen.slices;
				if( ImGui::MenuItem( "Up (drawn over)", nullptr, false, k + 1 < (int)slices.size() ) )
				{
					std::swap( slices[ k ], slices[ k + 1 ] );
					editor.slice  = k + 1;
					editor.picked = { k + 1 };
				}
				if( ImGui::MenuItem( "Down (drawn under)", nullptr, false, k > 0 ) )
				{
					std::swap( slices[ k ], slices[ k - 1 ] );
					editor.slice  = k - 1;
					editor.picked = { k - 1 };
				}
				if( ImGui::MenuItem( "Duplicate" ) )
				{
					Slice copy = slices[ k ];
					copy.name  = UniqueName( slices, copy.mask ? "Mask" : copy.polygon ? "Polygon" : "Slice" );
					copy.id.clear(); // a new one
					slices.insert( slices.begin() + k + 1, copy );
					setup.EnsureIds();
					editor.slice = k + 1;
				}
				ImGui::Separator();
				if( ImGui::MenuItem( "Delete" ) )
				{
					slices.erase( slices.begin() + k );
					editor.slice = std::min( k, (int)slices.size() - 1 );
					ImGui::EndPopup();
					ImGui::PopID();
					break;
				}
				ImGui::EndPopup();
			}
			ImGui::PopID();
		}
		if( screen.slices.empty() )
			ImGui::TextDisabled( "Nothing on this screen yet:\nadd a slice below." );
	}
	ImGui::EndChild();

	// Order and copies of the selection; new slices, polygons and masks.
	haveScreen     = editor.screen >= 0 && editor.screen < (int)setup.screens.size();
	bool haveSlice = haveScreen && editor.slice >= 0 && editor.slice < (int)setup.screens[ editor.screen ].slices.size();
	float quarter  = ( ImGui::GetContentRegionAvail().x - 3 * ImGui::GetStyle().ItemSpacing.x ) / 4;
	ImGui::BeginDisabled( !haveSlice );
	if( ImGui::Button( "Up", ImVec2( quarter, 0 ) ) && haveSlice && editor.slice + 1 < (int)setup.screens[ editor.screen ].slices.size() )
	{
		std::swap( setup.screens[ editor.screen ].slices[ editor.slice ], setup.screens[ editor.screen ].slices[ editor.slice + 1 ] );
		editor.slice++;
		editor.picked = { editor.slice };
	}
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "Drawn over the one above it" );
	ImGui::SameLine();
	if( ImGui::Button( "Down", ImVec2( quarter, 0 ) ) && haveSlice && editor.slice > 0 )
	{
		std::swap( setup.screens[ editor.screen ].slices[ editor.slice ], setup.screens[ editor.screen ].slices[ editor.slice - 1 ] );
		editor.slice--;
		editor.picked = { editor.slice };
	}
	ImGui::SameLine();
	if( ImGui::Button( "Copy", ImVec2( quarter, 0 ) ) && haveSlice )
	{
		std::vector< Slice >& slices = setup.screens[ editor.screen ].slices;
		Slice copy                   = slices[ editor.slice ];
		copy.name                    = UniqueName( slices, copy.mask ? "Mask" : copy.polygon ? "Polygon" : "Slice" );
		copy.id.clear(); // a new one
		slices.insert( slices.begin() + editor.slice + 1, copy );
		setup.EnsureIds();
		editor.slice++;
	}
	ImGui::SameLine();
	if( ImGui::Button( "Delete", ImVec2( quarter, 0 ) ) && haveSlice )
	{
		// Every picked slice.
		std::vector< Slice >& slices = setup.screens[ editor.screen ].slices;
		std::vector< int > gone      = editor.picked;
		std::sort( gone.rbegin(), gone.rend() );
		for( int i : gone )
			if( i >= 0 && i < (int)slices.size() )
				slices.erase( slices.begin() + i );
		editor.picked.clear();
		editor.slice = std::min( gone.empty() ? editor.slice : gone.back(), (int)slices.size() - 1 );
	}
	if( ImGui::IsItemHovered() && editor.picked.size() > 1 )
		ImGui::SetTooltip( "Deletes the %d picked slices", (int)editor.picked.size() );
	ImGui::EndDisabled();

	float third = ( ImGui::GetContentRegionAvail().x - 2 * ImGui::GetStyle().ItemSpacing.x ) / 3;
	ImGui::BeginDisabled( !haveScreen );
	if( ImGui::Button( "+ Slice", ImVec2( third, 0 ) ) && haveScreen )
	{
		Screen& s = setup.screens[ editor.screen ];
		s.slices.push_back( AdvancedOutputSetup::MakeSlice( UniqueName( s.slices, "Slice" ), cw, ch, (float)s.width, (float)s.height ) );
		editor.slice = (int)s.slices.size() - 1;
	}
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "A rectangle of the composition, shaped onto the screen" );
	ImGui::SameLine();
	if( ImGui::Button( "+ Polygon", ImVec2( third, 0 ) ) && haveScreen )
		ImGui::OpenPopup( "##newPolygon" );
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "Any shape of the composition, point by point: draw it, or start from a regular shape" );
	if( ImGui::BeginPopup( "##newPolygon" ) )
	{
		if( ImGui::MenuItem( "Draw..." ) && haveScreen )
		{
			editor.drawing    = true;
			editor.drawScreen = editor.screen;
			editor.drawTab    = editor.view == Editor::View::Both ? -1 : editor.tab; // side by side: the pane clicked first
			editor.drawPoints.clear();
		}
		if( ImGui::IsItemHovered() )
			ImGui::SetTooltip( "Click the points on a canvas (Takes from or Goes to); click the first point, double-click or press "
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
	ImGui::SameLine();
	if( ImGui::Button( "+ Mask", ImVec2( third, 0 ) ) && haveScreen )
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
}

// The presets and Resolume's setups, in the header's Presets menu.
void DrawPresets( App& app )
{

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
void DrawPolygonTool( App& app, Screen& screen, int tab, ImDrawList* draw, ImVec2 pos, ImVec2 size, ToScreen toScreen, ToContent toContent,
                      float scale, float W, float H, bool hovered )
{
	ImGuiIO& io              = ImGui::GetIO();
	std::vector< Vec2 >& pts = editor.drawPoints;
	Vec2 mouse               = Snap( toContent( io.MousePos ), screen, tab, -1, W, H, Ui( 8 ) / scale );
	const ImU32 amber        = IM_COL32( 255, 190, 70, 255 ), black = IM_COL32( 0, 0, 0, 255 );
	float radius             = Ui( 4.5f );
	auto atMouse              = [ & ]( Vec2 p ) {
        ImVec2 a = toScreen( p );
        return ( io.MousePos.x - a.x ) * ( io.MousePos.x - a.x ) + ( io.MousePos.y - a.y ) * ( io.MousePos.y - a.y ) < radius * radius * 4;
	};
	bool closable = pts.size() >= 3 && atMouse( pts[ 0 ] );

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
			app.EditedOutput().SourceSize( editor.inputSource, app.composition.width, app.composition.height, sw, sh );
			screen.slices.push_back( AdvancedOutputSetup::MakePolygonFrom( UniqueName( screen.slices, "Polygon" ), pts, tab == OutputTab,
			                                                               (float)sw, (float)sh, (float)screen.width, (float)screen.height ) );
			screen.slices.back().source = editor.inputSource;
			editor.slice = (int)screen.slices.size() - 1;
			editor.point = -1;
		}
		editor.drawing = false;
		pts.clear();
	};
	bool clicked = hovered && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) && !ImGui::IsKeyDown( ImGuiKey_Space );
	bool keys    = !io.WantTextInput; // not while a name or a number is being typed elsewhere
	if( clicked && ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) )
		finish(); // its first click already placed the last point
	else if( clicked && closable )
		finish();
	else if( clicked )
	{
		bool repeat = !pts.empty() && atMouse( pts.back() ); // the same place twice adds nothing
		if( !repeat )
		{
			pts.push_back( mouse );
			editor.drawTab = tab; // drawn on this pane from now on
		}
	}
	else if( keys && ( ImGui::IsKeyPressed( ImGuiKey_Enter ) || ImGui::IsKeyPressed( ImGuiKey_KeypadEnter ) ) )
		finish();
	else if( keys && ( ImGui::IsKeyPressed( ImGuiKey_Backspace ) || ImGui::IsKeyPressed( ImGuiKey_Delete ) ) && !pts.empty() )
		pts.pop_back();
	else if( ( keys && ImGui::IsKeyPressed( ImGuiKey_Escape ) ) || ( hovered && ImGui::IsMouseClicked( ImGuiMouseButton_Right ) ) )
	{
		editor.drawing = false;
		pts.clear();
	}
}

void DrawCanvas( App& app, Screen& screen, int tab )
{
	ImGuiIO& io = ImGui::GetIO();
	SyncPicked( screen );
	// The Input tab shows the picture the selected slice takes from (the composition or a canvas).
	if( editor.slice >= 0 && editor.slice < (int)screen.slices.size() && !screen.slices[ editor.slice ].mask )
		editor.inputSource = screen.slices[ editor.slice ].source;
	if( !app.EditedOutput().FindCanvas( editor.inputSource ) )
		editor.inputSource.clear();
	int sourceWidth, sourceHeight;
	app.EditedOutput().SourceSize( editor.inputSource, app.composition.width, app.composition.height, sourceWidth, sourceHeight );
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
	if( hovered && ( ImGui::IsMouseClicked( ImGuiMouseButton_Left ) || ImGui::IsMouseClicked( ImGuiMouseButton_Right ) ) )
		editor.tab = tab; // the pane in use: keys and the inspector's point rows follow it
	ImDrawList* draw = ImGui::GetWindowDrawList();
	draw->PushClipRect( pos, ImVec2( pos.x + size.x, pos.y + size.y ), true );
	draw->AddRectFilled( pos, ImVec2( pos.x + size.x, pos.y + size.y ), IM_COL32( 22, 22, 22, 255 ) );
	if( editor.tab == tab && hovered )
		draw->AddRect( pos, ImVec2( pos.x + size.x, pos.y + size.y ), theme::AccentDim, 0, 0, Ui( 1 ) );

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

	GLuint texture = tab == OutputTab                ? app.EditedRenderer().ScreenTexture( (size_t)editor.screen )
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
		// Each slice in its own colour, the same in the list and on both canvases; the selected one bolder.
		ImU32 colour = !s.enabled ? IM_COL32( 140, 140, 140, 90 )
		               : s.mask   ? IM_COL32( 255, 110, 110, 170 ) // masks in red
		                          : ( SliceColour( i ) & 0x00FFFFFF ) | ( IsPicked( i ) ? 0xFF000000 : 0xB4000000 );
		if( IsPicked( i ) && i != selected )
			draw->AddPolyline( pts.data(), (int)pts.size(), IM_COL32( 255, 255, 255, 200 ), ImDrawFlags_Closed, Ui( 3.5f ) );
		draw->AddPolyline( pts.data(), (int)pts.size(), colour, ImDrawFlags_Closed, IsPicked( i ) ? Ui( 2.5f ) : Ui( 1.2f ) );
		if( !s.mask )
		{
			// Its number at its top left corner, as in the list.
			ImVec2 corner = pts[ 0 ];
			for( ImVec2 p : pts )
				if( p.x + p.y < corner.x + corner.y )
					corner = p;
			std::string number = std::to_string( i + 1 );
			Badge( draw, corner, number.c_str(), s.enabled ? SliceColour( i ) : IM_COL32( 110, 110, 110, 255 ), Ui( 15 ) );
		}
		ImVec2 c( 0, 0 );
		for( ImVec2 p : pts )
		{
			c.x += p.x / pts.size();
			c.y += p.y / pts.size();
		}
		// The name in the middle, when it fits.
		ImVec2 text = ImGui::CalcTextSize( s.name.c_str() );
		float minX = pts[ 0 ].x, maxX = pts[ 0 ].x;
		for( ImVec2 p : pts )
		{
			minX = std::min( minX, p.x );
			maxX = std::max( maxX, p.x );
		}
		if( text.x < ( maxX - minX ) * 0.8f )
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
	if( editor.drag != Drag::None && editor.dragTab == tab && ( editor.snappedX || editor.snappedY ) )
	{
		ImVec2 p = toScreen( editor.snapAt );
		if( editor.snappedX )
			draw->AddLine( ImVec2( p.x, pos.y ), ImVec2( p.x, pos.y + size.y ), IM_COL32( 255, 200, 80, 160 ) );
		if( editor.snappedY )
			draw->AddLine( ImVec2( pos.x, p.y ), ImVec2( pos.x + size.x, p.y ), IM_COL32( 255, 200, 80, 160 ) );
	}

	// A polygon being drawn: on the pane its first point went to (either, until then); on another screen it is
	// given up; the other pane only shows while drawing.
	if( editor.drawing && editor.drawScreen != editor.screen )
	{
		editor.drawing = false;
		editor.drawPoints.clear();
	}
	if( editor.drawing && ( editor.drawTab < 0 || editor.drawTab == tab ) )
		return DrawPolygonTool( app, screen, tab, draw, pos, size, toScreen, toContent, scale, W, H, hovered );
	if( editor.drawing )
	{
		draw->PopClipRect();
		return;
	}
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
		editor.drag    = Drag::None;
		editor.dragTab = tab;
		editor.handle  = -1;
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
			bool adding  = io.KeyCtrl || io.KeyShift;
			editor.point = -1;
			if( hit < 0 )
			{
				// On an empty part: a box to pick slices with (Ctrl or Shift: added to those picked).
				editor.drag         = Drag::Pick;
				editor.pickFrom     = mouse;
				if( !adding )
				{
					editor.slice = -1;
					editor.picked.clear();
				}
				selected = editor.slice;
			}
			else if( adding )
			{
				// Ctrl or Shift + click: in or out of the picked slices.
				if( IsPicked( hit ) && editor.picked.size() > 1 )
				{
					editor.picked.erase( std::find( editor.picked.begin(), editor.picked.end(), hit ) );
					editor.slice = editor.picked.back();
				}
				else
				{
					if( !IsPicked( hit ) )
						editor.picked.push_back( hit );
					editor.slice = hit;
				}
				selected = editor.slice;
			}
			else
			{
				// A click on one of several picked slices keeps them all, to drag them together.
				if( !IsPicked( hit ) )
					editor.picked = { hit };
				editor.slice = hit;
				selected     = hit;
				editor.drag  = Drag::Move;
			}
		}
		if( editor.drag != Drag::None && editor.drag != Drag::Pick && selected >= 0 )
		{
			editor.dragFrom = mouse;
			BeginEdit( screen.slices[ selected ], tab );
			editor.beforeMany.clear();
			if( editor.drag == Drag::Move )
				for( int i : editor.picked )
				{
					// Only those on this canvas: on the input side, slices taking from another picture stay.
					if( i < 0 || i >= (int)screen.slices.size() || !onCanvas( screen.slices[ i ] ) )
						continue;
					std::vector< Vec2 > points;
					for( Vec2* q : EditablePoints( screen.slices[ i ], tab ) )
						points.push_back( *q );
					editor.beforeMany.push_back( { i, points } );
				}
		}
	}
	if( editor.drag == Drag::Pick && editor.dragTab == tab )
	{
		// The box: every slice whose outline it touches is picked when the button comes up.
		float x0 = std::min( editor.pickFrom.x, mouse.x ), y0 = std::min( editor.pickFrom.y, mouse.y );
		float x1 = std::max( editor.pickFrom.x, mouse.x ), y1 = std::max( editor.pickFrom.y, mouse.y );
		draw->AddRectFilled( toScreen( { x0, y0 } ), toScreen( { x1, y1 } ), ( theme::Accent & 0x00FFFFFF ) | 0x22000000 );
		draw->AddRect( toScreen( { x0, y0 } ), toScreen( { x1, y1 } ), theme::Accent );
		if( !ImGui::IsMouseDown( ImGuiMouseButton_Left ) && ( x1 - x0 ) * scale > 3 && ( y1 - y0 ) * scale > 3 )
			for( int i = 0; i < (int)screen.slices.size(); ++i )
			{
				if( !onCanvas( screen.slices[ i ] ) || IsPicked( i ) )
					continue;
				float a0, b0, a1, b1;
				Bounds( Outline( screen.slices[ i ], tab ), a0, b0, a1, b1 );
				if( a0 < x1 && a1 > x0 && b0 < y1 && b1 > y0 )
				{
					editor.picked.push_back( i );
					editor.slice = i;
				}
			}
	}
	if( !ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
	{
		editor.drag     = Drag::None;
		editor.snappedX = editor.snappedY = false;
	}
	if( editor.drag != Drag::None && editor.drag != Drag::Pick && editor.dragTab == tab && selected >= 0 && ImGui::IsMouseDragging( ImGuiMouseButton_Left, 1.0f ) )
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
				// The other picked slices move by as much.
				for( const auto& [ index, was ] : editor.beforeMany )
				{
					if( index == selected || index >= (int)screen.slices.size() )
						continue;
					std::vector< Vec2* > others = EditablePoints( screen.slices[ index ], tab );
					if( others.size() == was.size() )
						for( size_t k = 0; k < others.size(); ++k )
							*others[ k ] = { was[ k ].x + delta.x, was[ k ].y + delta.y };
				}
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
		if( ( nudge.x != 0 || nudge.y != 0 ) && onePoint )
		{
			points[ editor.point ]->x += nudge.x;
			points[ editor.point ]->y += nudge.y;
		}
		else if( nudge.x != 0 || nudge.y != 0 )
			for( int i : editor.picked )
				if( i < (int)screen.slices.size() && onCanvas( screen.slices[ i ] ) )
					for( Vec2* q : EditablePoints( screen.slices[ i ], tab ) )
					{
						q->x += nudge.x;
						q->y += nudge.y;
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
			{
				int w, h; // the picture it takes from: the composition or its canvas
				app.EditedOutput().SourceSize( s.source, app.composition.width, app.composition.height, w, h );
				s.MatchOutputToInput( (float)w, (float)h, (float)screen.width, (float)screen.height );
			}
			if( ImGui::MenuItem( "Fit Output to Screen" ) )
				s.FitOutput( (float)screen.width, (float)screen.height );
			if( !s.polygon && ImGui::MenuItem( s.source.empty() ? "Input: Whole Composition" : "Input: Whole Screen Picture" ) )
			{
				int w, h;
				app.EditedOutput().SourceSize( s.source, app.composition.width, app.composition.height, w, h );
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

// The chosen screen in step 1: where it goes, its size, what else it is sent to, its colour.
void DrawScreenProps( App& app, Screen& screen )
{
	AdvancedOutputSetup& setup = app.EditedOutput();
	{
		ImDrawList* draw = ImGui::GetWindowDrawList();
		ImVec2 p         = ImGui::GetCursorScreenPos();
		float h          = ImGui::GetFrameHeight();
		std::string number = std::to_string( editor.screen + 1 );
		float bw = Badge( draw, ImVec2( p.x, p.y + Ui( 2 ) ), number.c_str(), ScreenColour( editor.screen ), h - Ui( 4 ) );
		ImGui::SetCursorScreenPos( ImVec2( p.x + bw + Ui( 8 ), p.y ) );
		ImGui::AlignTextToFramePadding();
		ImGui::PushFont( Fonts().bold );
		ImGui::TextUnformatted( screen.name.c_str() );
		ImGui::PopFont();
	}
	ImGui::Spacing();
	if( !BeginProps( "##screen" ) )
		return;
	Row( "Name" );
	ImGui::InputText( "##name", &screen.name );
	Row( "Goes to" );
	std::string current = screen.device == OutputDevice::Display ? OutputWindow::DisplayLabel( screen.display )
	                      : screen.device == OutputDevice::Window ? "A window"
	                                                               : "Nowhere (only NDI or sharing)";
	if( ImGui::BeginCombo( "##device", current.c_str(), ImGuiComboFlags_HeightLarge ) )
	{
		for( const OutputWindow::Display& d : OutputWindow::Displays() )
		{
			std::string label = d.name + "   " + std::to_string( d.width ) + " x " + std::to_string( d.height );
			if( StltOnDisplay( app, d ) )
				label += "   (STLT is here)";
			if( ImGui::Selectable( label.c_str(), screen.device == OutputDevice::Display && OutputWindow::FindDisplay( screen.display ) == d.monitor ) )
				SendScreenTo( app, screen, editor.screen, d );
		}
		ImGui::Separator();
		if( ImGui::Selectable( "A window", screen.device == OutputDevice::Window ) )
			screen.device = OutputDevice::Window;
		if( ImGui::Selectable( "Nowhere (only NDI or sharing)", screen.device == OutputDevice::Virtual ) )
			screen.device = OutputDevice::Virtual;
		ImGui::EndCombo();
	}
	HelpRow( screen.device == OutputDevice::Display  ? "Fullscreen on that display."
	         : screen.device == OutputDevice::Window ? "A window you can move: to check the screen on your own monitor."
	                                                  : "Drawn here only, and sent by NDI or sharing when ticked below." );
	if( screen.device != OutputDevice::Virtual )
	{
		Row( "Now" );
		ImGui::PushTextWrapPos( 0 );
		ImGui::TextDisabled( "%s", app.EditedRenderer().DeviceStatus( (size_t)editor.screen ).c_str() );
		ImGui::PopTextWrapPos();
	}
	Row( "Size" );
	int size[ 2 ] = { screen.width, screen.height };
	if( CommittedInputInts( "##size", size, 2 ) )
	{
		screen.width  = std::clamp( size[ 0 ], 16, 16384 );
		screen.height = std::clamp( size[ 1 ], 16, 16384 );
	}
	if( screen.device == OutputDevice::Display )
		for( const OutputWindow::Display& d : OutputWindow::Displays() )
			if( d.monitor == OutputWindow::FindDisplay( screen.display ) && ( d.width != screen.width || d.height != screen.height ) )
			{
				Row( "" );
				char label[ 96 ];
				std::snprintf( label, sizeof( label ), "Use the display's size, %d x %d", d.width, d.height );
				if( ImGui::Button( label ) )
				{
					screen.width  = d.width;
					screen.height = d.height;
				}
			}
	Row( "Also send" );
	ImGui::Checkbox( "NDI##ndi", &screen.ndi );
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "Over the network as the NDI source \"STLT %s\"", screen.name.c_str() );
	if( const char* local = LocalSharingName() )
	{
		ImGui::SameLine( 0, Ui( 16 ) );
		ImGui::Checkbox( ( std::string( local ) + "##share" ).c_str(), &screen.share );
		if( ImGui::IsItemHovered() )
			ImGui::SetTooltip( "To other programs on this computer as \"STLT %s\"", screen.name.c_str() );
	}
	std::string sharing = app.EditedRenderer().SharingStatus( (size_t)editor.screen );
	if( !sharing.empty() )
		HelpRow( sharing.c_str() );
	Row( "On" );
	ImGui::Checkbox( "##enabled", &screen.enabled );
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "Off: the screen sends nothing, and keeps its settings" );
	if( app.outputTab == 1 )
	{
		// The timeline's tracks this screen shows: tick them (the same as choosing the screen next to a track's name).
		Row( "Tracks" );
		std::vector< TimelineTrack >& tracks = app.composition.timeline.tracks;
		std::string own = setup.ScreenPictureOf( (size_t)editor.screen );
		bool any        = false;
		for( int k = (int)tracks.size() - 1; k >= 0; --k )
		{
			if( tracks[ k ].audio )
				continue;
			any     = true;
			bool on = !own.empty() && tracks[ k ].output == own;
			if( ImGui::Checkbox( ( tracks[ k ].name + "##sendTrack" + std::to_string( k ) ).c_str(), &on ) )
				SendTrackTo( app, k, on ? editor.screen : -1 );
			if( ImGui::IsItemHovered() )
				ImGui::SetTooltip( on ? "Goes to this screen only. Untick: back to All Screens" : "Send this track to this screen only" );
		}
		if( !any )
			ImGui::TextDisabled( "No video tracks on the timeline" );
		else if( own.empty() )
			HelpRow( "None ticked: it shows the composition (All Screens)." );
	}
	ImGui::EndTable();

	ImGui::Spacing();
	SectionBar( "Colour", "for this screen only" );
	if( BeginProps( "##colour" ) )
	{
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
		Row( "Test pattern" );
		ImGui::Checkbox( "##test", &screen.testPattern );
		if( ImGui::IsItemHovered() )
			ImGui::SetTooltip( "Each slice drawn as a numbered grid, to line up projectors and LED walls" );
		ImGui::EndTable();
	}

	ImGui::Spacing();
	SectionBar( "What is on it" );
	int slices = 0, masks = 0;
	for( const Slice& sl : screen.slices )
		( sl.mask ? masks : slices )++;
	ImGui::PushStyleColor( ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4( theme::TextSoft ) );
	ImGui::TextWrapped( "%d slice%s%s.", slices, slices == 1 ? "" : "s", masks ? ( " and " + std::to_string( masks ) + ( masks == 1 ? " mask" : " masks" ) ).c_str() : "" );
	ImGui::PopStyleColor();
	if( ImGui::Button( "Map them  >" ) )
		editor.step = Editor::Step::Map;
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "3 Map: which part of the picture goes where on this screen" );

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();
	if( ImGui::Button( "Duplicate" ) )
	{
		Screen copy = screen;
		copy.name += " copy";
		copy.id.clear();
		for( Slice& sl : copy.slices )
			sl.id.clear();
		setup.screens.insert( setup.screens.begin() + editor.screen + 1, copy );
		setup.EnsureIds();
		ShowScreen( setup, editor.screen + 1 );
		return;
	}
	ImGui::SameLine();
	ImGui::BeginDisabled( setup.screens.size() <= 1 );
	if( ImGui::Button( "Delete" ) )
	{
		setup.screens.erase( setup.screens.begin() + editor.screen );
		editor.screen = std::max( 0, editor.screen - 1 );
		editor.slice  = -1;
		editor.point  = -1;
		editor.picked.clear();
	}
	ImGui::EndDisabled();
}

// The output mode and the selected point: shared by slices and masks.
void EditModeRows( Slice& s )
{
	if( editor.tab == OutputTab )
	{
		Row( "Drag" );
		if( ImGui::RadioButton( "Points", editor.outputEdit == OutputEdit::Points ) )
			editor.outputEdit = OutputEdit::Points;
		ImGui::SameLine();
		if( ImGui::RadioButton( "Move, size, turn", editor.outputEdit == OutputEdit::Transform ) )
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

// The selected item's name and kind, over its settings.
void ItemTitle( const Slice& s, int index )
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	ImVec2 p         = ImGui::GetCursorScreenPos();
	float h          = ImGui::GetFrameHeight();
	std::string number = s.mask ? "M" : std::to_string( index + 1 );
	float bw = Badge( draw, ImVec2( p.x, p.y + Ui( 2 ) ), number.c_str(), s.mask ? IM_COL32( 150, 150, 150, 255 ) : SliceColour( index ), h - Ui( 4 ) );
	ImGui::SetCursorScreenPos( ImVec2( p.x + bw + Ui( 8 ), p.y ) );
	ImGui::AlignTextToFramePadding();
	ImGui::PushFont( Fonts().bold );
	ImGui::TextUnformatted( s.name.c_str() );
	ImGui::PopFont();
	ImGui::SameLine();
	ImGui::TextDisabled( "%s", s.mask ? "mask" : s.polygon ? "polygon" : "slice" );
	ImGui::Spacing();
}

void DrawMaskProps( Screen& screen, Slice& s )
{
	ItemTitle( s, editor.slice );
	if( BeginProps( "##mask" ) )
	{
		Row( "Name" );
		ImGui::InputText( "##name", &s.name );
		Row( "On" );
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
		HelpRow( "Blacks out what is below it in the list." );
		ImGui::EndTable();
	}
	SectionBar( "Its shape", "screen pixels" );
	if( BeginProps( "##maskShape" ) )
	{
		EditModeRows( s );
		PointModeRows( s );
		Row( "" );
		if( ImGui::Button( "Smooth All" ) )
			for( int i = 0; i < (int)s.contour.size(); ++i )
				s.SmoothContourPoint( i );
		ImGui::SameLine();
		if( ImGui::Button( "Fill the Screen" ) )
			s.FitOutput( (float)screen.width, (float)screen.height );
		ImGui::EndTable();
	}
}

void DrawSliceProps( App& app, Screen& screen, Slice& s )
{
	if( s.mask )
	{
		DrawMaskProps( screen, s );
		return;
	}
	AdvancedOutputSetup& setup = app.EditedOutput();
	Composition& comp          = app.composition;
	int sourceW, sourceH;
	setup.SourceSize( s.source, comp.width, comp.height, sourceW, sourceH );
	float cw = (float)sourceW, ch = (float)sourceH; // the picture the slice takes from
	ItemTitle( s, editor.slice );
	if( editor.picked.size() > 1 )
	{
		ImGui::PushStyleColor( ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4( theme::Accent ) );
		ImGui::TextWrapped( "%d slices picked: dragging or the arrows move them all. Below: this one's settings.", (int)editor.picked.size() );
		ImGui::PopStyleColor();
	}
	if( BeginProps( "##slice" ) )
	{
		Row( "Name" );
		ImGui::InputText( "##name", &s.name );
		Row( "On" );
		ImGui::Checkbox( "##enabled", &s.enabled );
		ImGui::EndTable();
	}

	// 1: what it takes from.
	SectionBar( "1  Takes from", s.source.empty() ? "composition pixels" : "picture pixels" );
	if( BeginProps( "##takes" ) )
	{
		// The timeline's setup: what the slice takes from, the composition (the tracks sent to every screen) or a
		// screen's own picture (the tracks sent to that screen). The Grid's slices all take from the composition.
		if( app.outputTab == 1 )
		{
			Row( "Picture" );
			int self = editor.screen;
			auto label = [ & ]( const std::string& id ) {
				const Canvas* c = setup.FindCanvas( id );
				int owner       = setup.ScreenOfPicture( id );
				char text[ 200 ];
				if( !c )
					std::snprintf( text, sizeof( text ), "All Screens (composition)" );
				else if( owner == self )
					std::snprintf( text, sizeof( text ), "This screen's tracks" );
				else
					std::snprintf( text, sizeof( text ), "%s's tracks", owner >= 0 ? setup.screens[ owner ].name.c_str() : c->name.c_str() );
				return std::string( text );
			};
			if( ImGui::BeginCombo( "##source", label( s.source ).c_str() ) )
			{
				if( ImGui::Selectable( label( "" ).c_str(), s.source.empty() ) )
					setup.SetSliceSource( s, "", comp.width, comp.height );
				if( ImGui::IsItemHovered() )
					ImGui::SetTooltip( "The composition: the tracks set to All Screens" );
				std::string own = setup.ScreenPictureOf( (size_t)self );
				if( ImGui::Selectable( "This screen's tracks", !own.empty() && s.source == own ) )
				{
					own = setup.MakeScreenPicture( (size_t)self, comp.width, comp.height );
					setup.SetSliceSource( s, own, comp.width, comp.height );
				}
				if( ImGui::IsItemHovered() )
					ImGui::SetTooltip( "The tracks sent to %s, at its size", screen.name.c_str() );
				for( const Canvas& c : setup.canvases )
					if( c.id != own && ImGui::Selectable( ( label( c.id ) + "##" + c.id ).c_str(), s.source == c.id ) )
						setup.SetSliceSource( s, c.id, comp.width, comp.height );
				ImGui::EndCombo();
			}
			std::string fed;
			for( const TimelineTrack& t : comp.timeline.tracks )
				if( !t.audio && ( s.source.empty() ? !setup.FindCanvas( t.output ) : t.output == s.source ) )
					fed += ( fed.empty() ? "" : ", " ) + t.name;
			Row( "Tracks" );
			ImGui::TextWrapped( "%s", fed.empty() ? "none: choose this screen on a timeline track" : fed.c_str() );
		}
		if( s.polygon )
		{
			Row( "Points" );
			ImGui::Text( "%d", (int)s.input.size() );
			HelpRow( "Add a point: double-click the outline. Remove one: select it and press Del. A new shape: + Polygon > Draw." );
		}
		if( !s.polygon && s.input.size() == 4 )
		{
			float minX, minY, maxX, maxY;
			Bounds( s.input, minX, minY, maxX, maxY );
			float rect[ 4 ] = { minX, minY, maxX - minX, maxY - minY };
			Row( "Position" );
			bool changed = ImGui::DragFloat2( "##xy", rect, 1, -16384, 16384, "%.0f" );
			Row( "Size" );
			changed |= ImGui::DragFloat2( "##wh", rect + 2, 1, 1, 16384, "%.0f" );
			if( changed )
				SetRect( s, rect[ 0 ], rect[ 1 ], rect[ 0 ] + std::max( rect[ 2 ], 1.0f ), rect[ 1 ] + std::max( rect[ 3 ], 1.0f ) );
			Row( "" );
			if( ImGui::Button( s.source.empty() ? "The Whole Composition" : "The Whole Picture" ) )
				SetRect( s, 0, 0, cw, ch );
		}
		if( editor.tab == InputTab )
			EditModeRows( s );
		ImGui::EndTable();
	}

	// 2: where it goes, and its shape there.
	SectionBar( "2  Goes to", "screen pixels" );
	if( BeginProps( "##goes" ) )
	{
		if( editor.tab == OutputTab )
			EditModeRows( s );
		if( !s.polygon )
		{
			Row( "Shape" );
			if( ImGui::BeginCombo( "##warp", WarpModeName( s.warp ) ) )
			{
				for( int m = 0; m < (int)WarpMode::Count; ++m )
					if( ImGui::Selectable( WarpModeName( (WarpMode)m ), (int)s.warp == m ) )
						s.SetWarp( (WarpMode)m );
				ImGui::EndCombo();
			}
			HelpRow( s.warp == WarpMode::Perspective ? "Four corners: a rectangle for LED, or keystone for a projector."
			         : s.warp == WarpMode::Grid      ? "A grid of points joined by straight lines: folds and uneven surfaces."
			         : s.warp == WarpMode::Curved    ? "A grid of points joined by smooth curves: domes and curved screens."
			                                         : "Its outline, point by point, straight or curved: the picture stretched to fill it." );
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
				Row( "Grid points" );
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
		if( ImGui::Button( "Same as Taken" ) )
			s.MatchOutputToInput( cw, ch, (float)screen.width, (float)screen.height );
		if( ImGui::IsItemHovered() )
			ImGui::SetTooltip( "Where it is in the picture, scaled to the screen" );
		ImGui::SameLine();
		if( ImGui::Button( "Fill the Screen" ) )
			s.FitOutput( (float)screen.width, (float)screen.height );
		ImGui::EndTable();
	}

	SectionBar( "Look" );
	if( BeginProps( "##look" ) )
	{
		Row( "Opacity" );
		SliderPercent( "##opacity", &s.opacity, 0, 1, 1 );
		Row( "Flip" );
		ImGui::Checkbox( "Horizontal", &s.flipHorizontal );
		ImGui::SameLine();
		ImGui::Checkbox( "Vertical", &s.flipVertical );
		ImGui::EndTable();
	}

	if( ImGui::CollapsingHeader( "Soft edge  (overlapping projectors)", s.edge.Active() ? ImGuiTreeNodeFlags_DefaultOpen : 0 ) && BeginProps( "##edge" ) )
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

ImU32 ScreenColour( int index )
{
	static const ImU32 palette[] = { IM_COL32( 245, 184, 74, 255 ), IM_COL32( 230, 130, 175, 255 ), IM_COL32( 120, 205, 140, 255 ),
		                             IM_COL32( 180, 150, 240, 255 ), IM_COL32( 100, 205, 210, 255 ), IM_COL32( 235, 120, 95, 255 ) };
	if( index < 0 )
		return theme::Accent;
	return palette[ index % ( sizeof( palette ) / sizeof( palette[ 0 ] ) ) ];
}

void SendTrackTo( App& app, int track, int screen )
{
	AdvancedOutputSetup& setup = app.timelineOutput;
	std::vector< TimelineTrack >& tracks = app.composition.timeline.tracks;
	if( track < 0 || track >= (int)tracks.size() )
		return;
	// Choosing a track's screen is a choice of its own (it may give a screen its picture): saved at once, unless the
	// editor holds unsaved edits.
	bool clean = !app.OutputUnsaved();
	std::string before = tracks[ track ].output;
	tracks[ track ].output = screen >= 0 && screen < (int)setup.screens.size()
	                             ? setup.MakeScreenPicture( (size_t)screen, app.composition.width, app.composition.height )
	                             : "";
	// The picture it left, when no track goes there any more: its screen shows the composition again.
	if( !before.empty() && before != tracks[ track ].output && setup.ScreenOfPicture( before ) >= 0
	    && std::none_of( tracks.begin(), tracks.end(), [ & ]( const TimelineTrack& t ) { return t.output == before; } ) )
		setup.RemoveCanvas( before, app.composition.width, app.composition.height );
	if( clean && app.OutputUnsaved() )
		app.SaveOutputSetup();
}

int TrackScreen( const App& app, int track )
{
	const std::vector< TimelineTrack >& tracks = app.composition.timeline.tracks;
	if( track < 0 || track >= (int)tracks.size() )
		return -1;
	return app.timelineOutput.ScreenOfPicture( tracks[ track ].output );
}

// What timeline tracks go to: pictures of the timeline's setup, coloured and named after their screens.
ImU32 PictureColour( const App& app, const std::string& source )
{
	const AdvancedOutputSetup& setup = app.timelineOutput;
	if( !setup.FindCanvas( source ) )
		return theme::Accent;
	int screen = setup.ScreenOfPicture( source );
	if( screen >= 0 )
		return ScreenColour( screen );
	for( size_t i = 0; i < setup.canvases.size(); ++i )
		if( setup.canvases[ i ].id == source )
			return ScreenColour( (int)( setup.screens.size() + i ) );
	return theme::Accent;
}

std::string PictureName( const App& app, const std::string& source )
{
	const AdvancedOutputSetup& setup = app.timelineOutput;
	const Canvas* c = setup.FindCanvas( source );
	if( !c )
		return "All Screens";
	int screen = setup.ScreenOfPicture( source );
	return screen >= 0 ? setup.screens[ screen ].name : c->name;
}

void ShowAdvancedOutputTab( int tab, int screen, int slice )
{
	editor.step   = Editor::Step::Map;
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

// ---- the checks: what may be wrong with a screen, in words.

struct Problem
{
	bool serious;
	std::string text;
};

// A slice's shape on the screen, worked out once for all the checks.
struct Shape
{
	std::vector< Vec2 > outline;
	float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
	bool rect = false;
};

Shape ShapeOf( const Slice& s )
{
	Shape shape;
	shape.outline = s.OutputOutline( 4 );
	Bounds( shape.outline, shape.x0, shape.y0, shape.x1, shape.y1 );
	shape.rect = IsAxisAlignedRect( shape.outline );
	return shape;
}

// Whether two slices' shapes on the screen overlap, and by how much (their bounds' overlap, in pixels).
bool Overlap( const Shape& a, const Shape& b, float& w, float& h )
{
	const std::vector< Vec2 >&A = a.outline, &B = b.outline;
	if( A.size() < 3 || B.size() < 3 )
		return false;
	float x0 = std::max( a.x0, b.x0 ), y0 = std::max( a.y0, b.y0 ), x1 = std::min( a.x1, b.x1 ), y1 = std::min( a.y1, b.y1 );
	w = x1 - x0;
	h = y1 - y0;
	if( w < 1 || h < 1 )
		return false;
	if( a.rect && b.rect )
		return true;
	// Other shapes: a point in both, on a grid over the shared bounds.
	for( int i = 0; i <= 10; ++i )
		for( int j = 0; j <= 10; ++j )
		{
			Vec2 p{ x0 + w * ( i + 0.5f ) / 11, y0 + h * ( j + 0.5f ) / 11 };
			if( PointInPolygon( A, p ) && PointInPolygon( B, p ) )
				return true;
		}
	return false;
}

std::vector< Problem > FindProblems( const App& app, const AdvancedOutputSetup& setup, int screenIndex )
{
	std::vector< Problem > found;
	const Screen& screen = setup.screens[ screenIndex ];
	char text[ 300 ];
	if( !screen.enabled )
		found.push_back( { false, "This screen is off: it sends nothing (1 Screens)." } );
	else if( screen.device == OutputDevice::Virtual && !screen.ndi && !screen.share )
		found.push_back( { true, "This screen goes nowhere: choose a display or a window in 1 Screens, or tick NDI." } );
	if( screen.device == OutputDevice::Display )
	{
		bool connected = false;
		for( const OutputWindow::Display& d : OutputWindow::Displays() )
			if( d.monitor == OutputWindow::FindDisplay( screen.display ) )
			{
				connected = true;
				if( d.width != screen.width || d.height != screen.height )
				{
					std::snprintf( text, sizeof( text ), "The screen is %d x %d, its display %d x %d: the picture is scaled (1 Screens: use the display's size).",
					               screen.width, screen.height, d.width, d.height );
					found.push_back( { false, text } );
				}
			}
		if( !connected )
			found.push_back( { true, "Its display is not connected: the screen waits for it." } );
	}
	const std::vector< Slice >& slices = screen.slices;
	std::vector< Shape > shapes;
	shapes.reserve( slices.size() );
	for( const Slice& sl : slices )
		shapes.push_back( sl.enabled && !sl.mask ? ShapeOf( sl ) : Shape{} );
	int overlaps = 0;
	for( size_t i = 0; i < slices.size(); ++i )
	{
		const Slice& a = slices[ i ];
		if( !a.enabled || a.mask )
			continue;
		int pw, ph;
		setup.SourceSize( a.source, app.composition.width, app.composition.height, pw, ph );
		bool outside = false;
		for( Vec2 p : a.input )
			outside |= p.x < -0.5f || p.y < -0.5f || p.x > pw + 0.5f || p.y > ph + 0.5f;
		if( outside )
		{
			std::snprintf( text, sizeof( text ), "%zu %s takes from outside the picture: that part is black.", i + 1, a.name.c_str() );
			found.push_back( { false, text } );
		}
		const Shape& sa = shapes[ i ];
		if( sa.x1 <= 0 || sa.y1 <= 0 || sa.x0 >= screen.width || sa.y0 >= screen.height )
		{
			std::snprintf( text, sizeof( text ), "%zu %s is off the screen: nothing of it shows.", i + 1, a.name.c_str() );
			found.push_back( { true, text } );
		}
		for( size_t j = i + 1; j < slices.size() && overlaps < 6; ++j )
		{
			const Slice& b = slices[ j ];
			float w, h;
			if( !b.enabled || b.mask || !Overlap( sa, shapes[ j ], w, h ) )
				continue;
			std::snprintf( text, sizeof( text ), "%zu %s and %zu %s overlap on the screen (%.0f x %.0f px): %s is drawn over it.", i + 1, a.name.c_str(), j + 1,
			               b.name.c_str(), w, h, b.name.c_str() );
			found.push_back( { false, text } );
			overlaps++;
		}
	}
	return found;
}

// ---- the header, the steps, the foot

void DrawHeader( App& app )
{
	bool arrangement = app.view == App::View::Arrangement;
	ImGui::PushFont( Fonts().bold );
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted( "Setup" );
	ImGui::PopFont();
	ImGui::SameLine( 0, Ui( 10 ) );
	const char* names[] = { "Grid setup", "Timeline setup" };
	for( int t = 0; t < 2; ++t )
	{
		if( t > 0 )
			ImGui::SameLine( 0, 0 );
		bool on   = app.outputTab == t;
		bool live = ( t == 1 ) == arrangement;
		std::string text = std::string( names[ t ] ) + ( live ? "  \xE2\x80\xA2" : "" ) + "##setupTab" + std::to_string( t );
		if( on )
		{
			ImGui::PushStyleColor( ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4( theme::Accent ) );
			ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4( theme::Accent ) );
			ImGui::PushStyleColor( ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4( theme::OnAccent ) );
			ImGui::PushStyleColor( ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4( theme::Accent ) );
		}
		if( ImGui::Button( text.c_str() ) && !on )
		{
			app.outputTab = t;
			editor.screen = 0;
			editor.slice  = -1;
			editor.inputSource.clear();
		}
		if( on )
			ImGui::PopStyleColor( 4 );
		if( ImGui::IsItemHovered() )
			ImGui::SetTooltip( t == 0 ? "The Grid's setup: used in the Grid view.\nEvery layer goes to the composition, which the screens' slices cut up."
			                          : "The Timeline's setup: used in the Timeline view.\nEach track goes to every screen (the composition) or to one screen: "
			                            "choose it next to the track's name, or in 1 Screens." );
	}
	ImGui::SameLine( 0, Ui( 12 ) );
	ImGui::AlignTextToFramePadding();
	ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( theme::TextFaint ), "The Grid view uses the first, the Timeline view the second;  \xE2\x80\xA2  the one in use now" );
	float presetsW = ImGui::CalcTextSize( "Presets and imports" ).x + ImGui::GetStyle().FramePadding.x * 2;
	ImGui::SameLine( std::max( ImGui::GetCursorPosX() + Ui( 12 ), ImGui::GetWindowContentRegionMax().x - presetsW ) );
	if( ImGui::Button( "Presets and imports" ) )
		ImGui::OpenPopup( "##presets" );
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "Load or save the whole setup under a name; import Resolume's AdvancedOutput.xml" );
	ImGui::SetNextWindowSize( ImVec2( Ui( 320 ), 0 ) );
	if( ImGui::BeginPopup( "##presets" ) )
	{
		DrawPresets( app );
		ImGui::EndPopup();
	}
}

// The three steps as a path, then whether the setup is on show.
void DrawSteps( App& app, AdvancedOutputSetup& setup )
{
	ImDrawList* draw = ImGui::GetWindowDrawList();
	ImVec2 origin    = ImGui::GetCursorScreenPos();
	float h = Ui( 52 ), width = ImGui::GetContentRegionAvail().x;
	draw->AddRectFilled( ImVec2( origin.x - ImGui::GetStyle().WindowPadding.x, origin.y ), ImVec2( origin.x + width + ImGui::GetStyle().WindowPadding.x, origin.y + h ),
	                     theme::Panel );
	draw->AddLine( ImVec2( origin.x - ImGui::GetStyle().WindowPadding.x, origin.y + h ), ImVec2( origin.x + width + ImGui::GetStyle().WindowPadding.x, origin.y + h ),
	               theme::Line );
	struct StepInfo
	{
		const char* title;
		const char* hint;
		Editor::Step step;
	};
	const StepInfo steps[] = { { "Screens", "Where the picture goes out", Editor::Step::Screens },
		                       { "Stage", "What it lights, in real size", Editor::Step::Stage },
		                       { "Map", "Which part of the picture goes where", Editor::Step::Map } };
	float x = origin.x;
	for( int i = 0; i < 3; ++i )
	{
		const StepInfo& st = steps[ i ];
		bool on            = editor.step == st.step;
		bool soon          = st.step == Editor::Step::Stage; // the stage in 3D comes next
		ImGui::PushFont( Fonts().bold );
		float titleW = ImGui::CalcTextSize( st.title ).x;
		ImGui::PopFont();
		float hintW = ImGui::CalcTextSize( st.hint ).x;
		float box = Ui( 24 ), w = box + Ui( 12 ) + std::max( titleW, hintW ) + Ui( 18 );
		ImGui::SetCursorScreenPos( ImVec2( x, origin.y ) );
		ImGui::PushID( i );
		bool clicked = ImGui::InvisibleButton( "##step", ImVec2( w, h ) );
		ImGui::PopID();
		bool hovered = ImGui::IsItemHovered();
		if( clicked && !soon )
			editor.step = st.step;
		if( hovered && soon )
			ImGui::SetTooltip( "Coming next: the stage in 3D, in millimetres, with the live picture on it" );
		ImVec2 b0( x + Ui( 6 ), origin.y + ( h - box ) / 2 );
		char number[ 4 ];
		std::snprintf( number, sizeof( number ), "%d", i + 1 );
		ImU32 ink = soon ? theme::TextFaint : on || hovered ? theme::Text : theme::TextSoft;
		if( on )
			draw->AddRectFilled( b0, ImVec2( b0.x + box, b0.y + box ), theme::Accent );
		else
			draw->AddRect( b0, ImVec2( b0.x + box, b0.y + box ), soon ? theme::Line : theme::Edge, 0, 0, Ui( 1.5f ) );
		ImGui::PushFont( Fonts().mono );
		ImVec2 nt = ImGui::CalcTextSize( number );
		draw->AddText( ImVec2( b0.x + ( box - nt.x ) / 2, b0.y + ( box - nt.y ) / 2 ), on ? theme::OnAccent : ink, number );
		ImGui::PopFont();
		float tx = b0.x + box + Ui( 10 );
		draw->AddText( Fonts().bold, Fonts().bold->FontSize * 1.08f, ImVec2( tx, origin.y + h / 2 - Ui( 16 ) ), ink, st.title );
		draw->AddText( ImVec2( tx, origin.y + h / 2 + Ui( 1 ) ), soon ? theme::TextFaint : theme::TextDim, soon ? "Coming next" : st.hint );
		if( on )
			draw->AddRectFilled( ImVec2( x, origin.y + h - Ui( 2 ) ), ImVec2( x + w, origin.y + h ), theme::Accent );
		x += w;
		if( i < 2 )
		{
			ImVec2 c( x + Ui( 6 ), origin.y + h / 2 );
			draw->AddLine( ImVec2( c.x - Ui( 3 ), c.y - Ui( 5 ) ), ImVec2( c.x + Ui( 2 ), c.y ), theme::TextFaint, Ui( 1.5f ) );
			draw->AddLine( ImVec2( c.x + Ui( 2 ), c.y ), ImVec2( c.x - Ui( 3 ), c.y + Ui( 5 ) ), theme::TextFaint, Ui( 1.5f ) );
			x += Ui( 14 );
		}
	}

	// Right: the test pattern on every screen, and whether the setup is on show, in words.
	bool live = app.outputMode == OutputMode::Advanced;
	std::string state, action;
	ImU32 dot = theme::Accent;
	if( !app.EditingLive() )
	{
		state  = app.outputTab == 1 ? "Not on show: the Timeline view uses this setup" : "Not on show: the Grid view uses this setup";
		action = app.outputTab == 1 ? "Go to the Timeline" : "Go to the Grid";
		dot    = theme::TextDim;
	}
	else if( !live )
	{
		state  = "Not on show: the output is " + app.OutputModeLabel();
		action = "Turn on";
		dot    = theme::TextDim;
	}
	else
	{
		state = "On show ";
		int shown = 0;
		for( size_t i = 0; i < setup.screens.size(); ++i )
			if( ScreenOnShow( app, setup.screens[ i ] ) )
			{
				state += ( shown++ ? "  \xC2\xB7  " : "  " ) + setup.screens[ i ].name + " \xE2\x86\x92 " + Destination( setup.screens[ i ] );
			}
		if( shown == 0 )
		{
			state = "On, but every screen goes nowhere: choose where in 1 Screens";
			dot   = theme::Warning;
		}
		action = "Turn off";
	}
	bool pattern = std::any_of( setup.screens.begin(), setup.screens.end(), []( const Screen& sc ) { return sc.testPattern; } );
	const ImGuiStyle& style = ImGui::GetStyle();
	float actionW  = ImGui::CalcTextSize( action.c_str() ).x + style.FramePadding.x * 2;
	float patternW = ImGui::CalcTextSize( "Test pattern" ).x + style.FramePadding.x * 2;
	float right    = origin.x + width;
	float chipMax  = right - actionW - patternW - Ui( 16 ) - x - Ui( 24 );
	// As wide as what is left after the steps: in a narrow window only its dot (the words in its tooltip).
	float chipW    = std::max( Ui( 22 ), std::min( ImGui::CalcTextSize( state.c_str() ).x + Ui( 28 ), chipMax ) );
	float fy       = origin.y + ( h - ImGui::GetFrameHeight() ) / 2;
	float cx       = right - actionW - Ui( 8 ) - chipW;
	ImGui::SetCursorScreenPos( ImVec2( cx - patternW - Ui( 8 ), fy ) );
	if( pattern )
	{
		ImGui::PushStyleColor( ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4( theme::Edge ) );
	}
	if( ImGui::Button( "Test pattern" ) )
		for( Screen& sc : setup.screens )
			sc.testPattern = !pattern;
	if( pattern )
		ImGui::PopStyleColor();
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "Every screen draws each slice as a numbered grid, to line up projectors and LED walls" );
	ImVec2 c0( cx, fy ), c1( cx + chipW, fy + ImGui::GetFrameHeight() );
	draw->AddRectFilled( c0, c1, theme::Well );
	draw->AddRect( c0, c1, theme::Edge );
	draw->AddCircleFilled( ImVec2( c0.x + Ui( 11 ), ( c0.y + c1.y ) / 2 ), Ui( 3.5f ), dot );
	draw->PushClipRect( c0, ImVec2( c1.x - Ui( 6 ), c1.y ), true );
	draw->AddText( ImVec2( c0.x + Ui( 21 ), c0.y + ( c1.y - c0.y - ImGui::GetTextLineHeight() ) / 2 ), live && app.EditingLive() ? theme::Text : theme::TextSoft, state.c_str() );
	draw->PopClipRect();
	ImGui::SetCursorScreenPos( c0 );
	ImGui::InvisibleButton( "##stateChip", ImVec2( chipW, c1.y - c0.y ) );
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "%s", state.c_str() );
	ImGui::SetCursorScreenPos( ImVec2( right - actionW, fy ) );
	if( ImGui::Button( action.c_str() ) )
	{
		if( !app.EditingLive() )
			app.SetView( app.outputTab == 1 ? App::View::Arrangement : App::View::Session );
		else if( !live )
			app.SetOutputMode( OutputMode::Advanced );
		else
			app.SetOutputMode( OutputMode::Off );
	}
	ImGui::SetCursorScreenPos( ImVec2( origin.x, origin.y + h + Ui( 6 ) ) );
}

void DrawFoot( App& app )
{
	ImGui::Separator();
	bool unsaved = app.OutputUnsaved();
	ImGui::AlignTextToFramePadding();
	if( unsaved )
	{
		ImVec2 p = ImGui::GetCursorScreenPos();
		ImGui::GetWindowDrawList()->AddCircleFilled( ImVec2( p.x + Ui( 4 ), p.y + ImGui::GetFrameHeight() / 2 ), Ui( 3.5f ), theme::Accent );
		ImGui::Dummy( ImVec2( Ui( 10 ), 0 ) );
		ImGui::SameLine();
		ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( theme::Accent ), "Unsaved changes" );
		ImGui::SameLine();
		ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( theme::TextFaint ), "the outputs already show them; Save keeps them for next time" );
	}
	else
		ImGui::TextDisabled( "Saved" );
	const ImGuiStyle& style = ImGui::GetStyle();
	float right = ImGui::CalcTextSize( "Discard" ).x + ImGui::CalcTextSize( "Save" ).x + style.FramePadding.x * 4 + style.ItemSpacing.x;
	ImGui::SameLine( std::max( ImGui::GetCursorPosX() + Ui( 12 ), ImGui::GetWindowContentRegionMax().x - right ) );
	ImGui::BeginDisabled( !unsaved );
	if( ImGui::Button( "Discard" ) )
		app.RevertOutputSetup();
	if( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
		ImGui::SetTooltip( "Back to the setup as it was last saved" );
	ImGui::SameLine();
	if( unsaved )
	{
		ImGui::PushStyleColor( ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4( theme::Accent ) );
		ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4( theme::AccentHover ) );
		ImGui::PushStyleColor( ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4( theme::OnAccent ) );
	}
	if( ImGui::Button( "Save" ) )
		app.SaveOutputSetup();
	if( unsaved )
		ImGui::PopStyleColor( 3 );
	ImGui::EndDisabled();
}

// ---- step 1: the screens, and this computer's displays

constexpr const char* kScreenPayload = "STLT_SCREEN";

void DrawScreensStep( App& app, AdvancedOutputSetup& setup, float height )
{
	if( !ImGui::BeginTable( "##screensStep", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, ImVec2( 0, height ) ) )
		return;
	ImGui::TableSetupColumn( "screens", ImGuiTableColumnFlags_WidthStretch );
	ImGui::TableSetupColumn( "props", ImGuiTableColumnFlags_WidthFixed, Ui( 340 ) );
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex( 0 );
	ImGui::BeginChild( "##outputs", ImVec2( 0, height ) );
	ImDrawList* draw = ImGui::GetWindowDrawList();

	// Your outputs: a row per screen.
	ImGui::PushFont( Fonts().bold );
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted( "Your outputs" );
	ImGui::PopFont();
	ImGui::SameLine();
	ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( theme::TextFaint ), "each screen is one picture STLT sends out" );
	float addW = ImGui::CalcTextSize( "+ Screen" ).x + ImGui::GetStyle().FramePadding.x * 2;
	ImGui::SameLine( ImGui::GetWindowContentRegionMax().x - addW );
	if( ImGui::Button( "+ Screen" ) )
	{
		// A new screen, the size of the composition, with one slice showing all of it.
		Composition& comp = app.composition;
		Screen s;
		s.name   = "Screen " + std::to_string( setup.screens.size() + 1 );
		s.width  = comp.width;
		s.height = comp.height;
		s.slices.push_back( AdvancedOutputSetup::MakeSlice( "Slice 1", (float)comp.width, (float)comp.height, (float)s.width, (float)s.height ) );
		setup.screens.push_back( s );
		setup.EnsureIds();
		editor.screen = (int)setup.screens.size() - 1;
		editor.slice  = 0;
	}
	ImGui::Spacing();
	float rowH = Ui( 50 );
	for( int si = 0; si < (int)setup.screens.size(); ++si )
	{
		Screen& screen = setup.screens[ si ];
		ImGui::PushID( si );
		ImVec2 p = ImGui::GetCursorScreenPos();
		float w  = ImGui::GetContentRegionAvail().x;
		if( ImGui::InvisibleButton( "##card", ImVec2( w, rowH ) ) )
			ShowScreen( setup, si );
		bool hot = ImGui::IsItemHovered();
		if( ImGui::IsItemClicked( ImGuiMouseButton_Right ) )
		{
			ShowScreen( setup, si );
			ImGui::OpenPopup( "##screenMenu" );
		}
		if( ImGui::BeginDragDropSource() )
		{
			ImGui::SetDragDropPayload( kScreenPayload, &si, sizeof( si ) );
			ImGui::Text( "%s: drop it on a display", screen.name.c_str() );
			ImGui::EndDragDropSource();
		}
		bool selected = editor.screen == si;
		ImVec2 q( p.x + w, p.y + rowH );
		draw->AddRectFilled( p, q, selected ? theme::Raised : hot ? IM_COL32( 26, 24, 21, 255 ) : theme::Panel );
		draw->AddRect( p, q, selected ? theme::Accent : theme::Line, 0, 0, selected ? Ui( 1.5f ) : Ui( 1 ) );
		std::string number = std::to_string( si + 1 );
		Badge( draw, ImVec2( p.x + Ui( 12 ), p.y + ( rowH - Ui( 22 ) ) / 2 ), number.c_str(), screen.enabled ? ScreenColour( si ) : IM_COL32( 110, 110, 110, 255 ), Ui( 22 ) );
		// What it sends now, small.
		float th = rowH - Ui( 14 ), tw = std::min( Ui( 72 ), th * screen.width / std::max( 1, screen.height ) );
		th       = std::min( th, tw * screen.height / std::max( 1, screen.width ) );
		ImVec2 t0( p.x + Ui( 44 ), p.y + ( rowH - th ) / 2 ), t1( t0.x + tw, t0.y + th );
		draw->AddRectFilled( t0, t1, IM_COL32( 0, 0, 0, 255 ) );
		if( GLuint texture = app.EditedRenderer().ScreenTexture( (size_t)si ) )
			draw->AddImage( TextureId( texture ), t0, t1, ImVec2( 0, 1 ), ImVec2( 1, 0 ) );
		draw->AddRect( t0, t1, theme::Line );
		float tx = p.x + Ui( 44 ) + Ui( 72 ) + Ui( 12 );
		draw->AddText( Fonts().bold, Fonts().bold->FontSize * 1.05f, ImVec2( tx, p.y + Ui( 8 ) ), screen.enabled ? theme::Text : theme::TextDim, screen.name.c_str() );
		int slices = 0, masks = 0;
		for( const Slice& sl : screen.slices )
			( sl.mask ? masks : slices )++;
		char detail[ 160 ];
		std::snprintf( detail, sizeof( detail ), "%d slice%s%s%s", slices, slices == 1 ? "" : "s", masks ? ( ", " + std::to_string( masks ) + " mask" + ( masks == 1 ? "" : "s" ) ).c_str() : "",
		               screen.enabled ? "" : "  \xC2\xB7  off" );
		draw->AddText( ImVec2( tx, p.y + Ui( 28 ) ), theme::TextDim, detail );
		// Right: where it goes, its size, whether it is on show.
		bool onShow       = ScreenOnShow( app, screen );
		const char* state = onShow ? "On show" : !screen.enabled ? "Off" : "Not on show";
		ImVec2 st         = ImGui::CalcTextSize( state );
		float sx          = q.x - Ui( 12 ) - st.x;
		draw->AddText( ImVec2( sx, p.y + ( rowH - st.y ) / 2 ), onShow ? theme::Accent : theme::TextDim, state );
		if( onShow )
			draw->AddCircleFilled( ImVec2( sx - Ui( 9 ), p.y + rowH / 2 ), Ui( 3.5f ), theme::Accent );
		char size[ 32 ];
		std::snprintf( size, sizeof( size ), "%d x %d", screen.width, screen.height );
		ImGui::PushFont( Fonts().mono );
		ImVec2 zt = ImGui::CalcTextSize( size );
		float zx  = sx - Ui( 30 ) - zt.x;
		draw->AddText( ImVec2( zx, p.y + ( rowH - zt.y ) / 2 ), theme::TextDim, size );
		ImGui::PopFont();
		std::string to = "\xE2\x86\x92  " + Destination( screen );
		ImGui::PushFont( Fonts().bold );
		ImVec2 dt = ImGui::CalcTextSize( to.c_str() );
		draw->AddText( ImVec2( zx - Ui( 24 ) - dt.x, p.y + ( rowH - dt.y ) / 2 ), screen.device == OutputDevice::Virtual && !screen.ndi && !screen.share ? theme::Warning : theme::Text,
		               to.c_str() );
		ImGui::PopFont();
		if( hot )
			ImGui::SetTooltip( "%s\nClick: its settings on the right. Drag it onto a display below. Right-click: on/off, duplicate, delete.", screen.name.c_str() );
		if( ImGui::BeginPopup( "##screenMenu" ) )
		{
			ImGui::MenuItem( "On", nullptr, &screen.enabled );
			if( ImGui::MenuItem( "Duplicate" ) )
			{
				Screen copy = screen;
				copy.name += " copy";
				copy.id.clear();
				for( Slice& sl : copy.slices )
					sl.id.clear();
				setup.screens.insert( setup.screens.begin() + si + 1, copy );
				setup.EnsureIds();
				ShowScreen( setup, si + 1 );
			}
			if( ImGui::MenuItem( "Delete", nullptr, false, setup.screens.size() > 1 ) )
			{
				setup.screens.erase( setup.screens.begin() + si );
				editor.screen = std::max( 0, si - 1 );
				editor.slice  = -1;
				editor.point  = -1;
				editor.picked.clear();
				ImGui::EndPopup();
				ImGui::PopID();
				break;
			}
			ImGui::EndPopup();
		}
		ImGui::PopID();
		ImGui::Dummy( ImVec2( 1, Ui( 2 ) ) );
	}

	// This computer's displays, as the system arranges them: drop a screen on one to send it there.
	ImGui::Spacing();
	ImGui::Spacing();
	ImGui::PushFont( Fonts().bold );
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted( "This computer's displays" );
	ImGui::PopFont();
	ImGui::SameLine();
	ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( theme::TextFaint ), "as the system arranges them: drag a screen onto a display, or choose it on the right" );
	ImVec2 area0 = ImGui::GetCursorScreenPos();
	ImVec2 areaSize( ImGui::GetContentRegionAvail().x, std::max( Ui( 170 ), ImGui::GetContentRegionAvail().y - Ui( 4 ) ) );
	draw->AddRectFilled( area0, ImVec2( area0.x + areaSize.x, area0.y + areaSize.y ), theme::Well );
	std::vector< OutputWindow::Display > displays = OutputWindow::Displays();
	float netW = Ui( 230 );
	ImVec2 dispArea( areaSize.x - netW - Ui( 36 ), areaSize.y - Ui( 32 ) );
	if( !displays.empty() && dispArea.x > 50 )
	{
		int minX = displays[ 0 ].x, minY = displays[ 0 ].y, maxX = minX + displays[ 0 ].width, maxY = minY + displays[ 0 ].height;
		for( const OutputWindow::Display& d : displays )
		{
			minX = std::min( minX, d.x );
			minY = std::min( minY, d.y );
			maxX = std::max( maxX, d.x + d.width );
			maxY = std::max( maxY, d.y + d.height );
		}
		float fit = std::min( dispArea.x / std::max( 1, maxX - minX ), dispArea.y / std::max( 1, maxY - minY ) );
		ImVec2 o( area0.x + Ui( 16 ) + ( dispArea.x - ( maxX - minX ) * fit ) / 2, area0.y + Ui( 16 ) + ( dispArea.y - ( maxY - minY ) * fit ) / 2 );
		for( int di = 0; di < (int)displays.size(); ++di )
		{
			const OutputWindow::Display& d = displays[ di ];
			ImVec2 a( o.x + ( d.x - minX ) * fit + Ui( 3 ), o.y + ( d.y - minY ) * fit + Ui( 3 ) );
			ImVec2 b( o.x + ( d.x + d.width - minX ) * fit - Ui( 3 ), o.y + ( d.y + d.height - minY ) * fit - Ui( 3 ) );
			ImGui::SetCursorScreenPos( a );
			ImGui::PushID( 1000 + di );
			bool clicked = ImGui::InvisibleButton( "##display", ImVec2( std::max( 1.0f, b.x - a.x ), std::max( 1.0f, b.y - a.y ) ) );
			bool hot     = ImGui::IsItemHovered();
			std::vector< int > here;
			for( int si = 0; si < (int)setup.screens.size(); ++si )
				if( setup.screens[ si ].device == OutputDevice::Display && OutputWindow::FindDisplay( setup.screens[ si ].display ) == d.monitor )
					here.push_back( si );
			if( ImGui::BeginDragDropTarget() )
			{
				if( const ImGuiPayload* payload = ImGui::AcceptDragDropPayload( kScreenPayload ) )
				{
					int si = *(const int*)payload->Data;
					if( si >= 0 && si < (int)setup.screens.size() )
					{
						SendScreenTo( app, setup.screens[ si ], si, d );
						ShowScreen( setup, si );
					}
				}
				ImGui::EndDragDropTarget();
			}
			if( clicked && !here.empty() )
				ShowScreen( setup, here[ 0 ] );
			bool ours = StltOnDisplay( app, d );
			ImU32 edge = here.empty() ? theme::Edge : ScreenColour( here[ 0 ] );
			draw->AddRectFilled( a, b, here.empty() ? theme::Raised : ( ( ScreenColour( here[ 0 ] ) & 0x00FFFFFF ) | 0x14000000 ) );
			draw->AddRect( a, b, hot ? theme::Text : edge, 0, 0, Ui( 1.5f ) );
			draw->PushClipRect( a, b, true );
			char label[ 64 ];
			std::snprintf( label, sizeof( label ), "%d", di + 1 );
			draw->AddText( Fonts().display, Fonts().display->FontSize, ImVec2( a.x + Ui( 8 ), a.y + Ui( 6 ) ), theme::TextDim, label );
			std::snprintf( label, sizeof( label ), "%d x %d", d.width, d.height );
			draw->AddText( Fonts().bold, Fonts().bold->FontSize, ImVec2( a.x + Ui( 8 ), a.y + Ui( 32 ) ), theme::Text, label );
			draw->AddText( ImVec2( a.x + Ui( 8 ), a.y + Ui( 50 ) ), theme::TextDim, d.monitorName.c_str() );
			if( ours )
				draw->AddText( ImVec2( a.x + Ui( 8 ), a.y + Ui( 68 ) ), theme::TextFaint, "STLT is here" );
			float bx = b.x - Ui( 6 );
			for( int si : here )
			{
				std::string n = std::to_string( si + 1 );
				ImFont* font  = Fonts().small;
				float bw      = std::max( Ui( 18 ), font->CalcTextSizeA( font->FontSize, FLT_MAX, 0, n.c_str() ).x + Ui( 8 ) );
				bx -= bw;
				Badge( draw, ImVec2( bx, a.y + Ui( 6 ) ), n.c_str(), ScreenColour( si ), Ui( 18 ) );
				bx -= Ui( 4 );
			}
			draw->PopClipRect();
			if( hot )
			{
				std::string names;
				for( int si : here )
					names += ( names.empty() ? "" : ", " ) + setup.screens[ si ].name;
				ImGui::SetTooltip( "%s\n%s", d.name.c_str(), here.empty() ? "Drop a screen here to send it to this display" : ( "Shows: " + names ).c_str() );
			}
			ImGui::PopID();
		}
	}
	else if( displays.empty() )
		draw->AddText( ImVec2( area0.x + Ui( 16 ), area0.y + Ui( 16 ) ), theme::TextDim, "No displays found" );
	{
		// The screens that go to a window, the network or other programs, or nowhere.
		ImVec2 a( area0.x + areaSize.x - netW - Ui( 16 ), area0.y + Ui( 16 ) ), b( a.x + netW, area0.y + areaSize.y - Ui( 16 ) );
		DrawDashedRect( draw, a, b, theme::Edge, 1.0f, Ui( 4 ) );
		draw->PushClipRect( a, b, true );
		float y = a.y + Ui( 8 );
		draw->AddText( Fonts().bold, Fonts().bold->FontSize, ImVec2( a.x + Ui( 10 ), y ), theme::Text, "Windows, network, apps" );
		y += Ui( 24 );
		const char* local = LocalSharingName();
		bool any          = false;
		for( int si = 0; si < (int)setup.screens.size(); ++si )
		{
			const Screen& sc = setup.screens[ si ];
			if( sc.device == OutputDevice::Display && !sc.ndi && !sc.share )
				continue;
			std::string where;
			if( sc.device == OutputDevice::Window )
				where = "Window";
			if( sc.ndi )
				where += std::string( where.empty() ? "" : " + " ) + "NDI";
			if( sc.share && local )
				where += std::string( where.empty() ? "" : " + " ) + local;
			if( where.empty() )
				where = "Nowhere yet";
			std::string n = std::to_string( si + 1 );
			Badge( draw, ImVec2( a.x + Ui( 10 ), y ), n.c_str(), ScreenColour( si ), Ui( 18 ) );
			draw->AddText( ImVec2( a.x + Ui( 36 ), y + Ui( 1 ) ), where == "Nowhere yet" ? theme::Warning : theme::TextSoft, ( where + "  \xC2\xB7  " + sc.name ).c_str() );
			y += Ui( 24 );
			any = true;
		}
		if( !any )
			draw->AddText( ImVec2( a.x + Ui( 10 ), y ), theme::TextFaint, "None" );
		draw->PopClipRect();
		ImGui::PushTextWrapPos( b.x - Ui( 10 ) - ImGui::GetWindowPos().x + ImGui::GetScrollX() );
		ImGui::SetCursorScreenPos( ImVec2( a.x + Ui( 10 ), b.y - Ui( 52 ) ) );
		ImGui::PushStyleColor( ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4( theme::TextFaint ) );
		if( local )
			ImGui::TextWrapped( "NDI sends a screen over the network; %s to other programs on this computer.", local );
		else
			ImGui::TextWrapped( "NDI sends a screen over the network, to other computers and programs." );
		ImGui::PopStyleColor();
		ImGui::PopTextWrapPos();
	}
	ImGui::SetCursorScreenPos( ImVec2( area0.x, area0.y + areaSize.y ) );
	ImGui::Dummy( ImVec2( 1, 1 ) );
	ImGui::EndChild();

	ImGui::TableSetColumnIndex( 1 );
	ImGui::BeginChild( "##screenProps", ImVec2( 0, height ) );
	editor.screen = std::clamp( editor.screen, 0, (int)setup.screens.size() - 1 );
	DrawScreenProps( app, setup.screens[ editor.screen ] );
	ImGui::EndChild();
	ImGui::EndTable();
}

// ---- step 3: the map

void DrawMapStep( App& app, AdvancedOutputSetup& setup, float height )
{
	if( !ImGui::BeginTable( "##ao", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, ImVec2( 0, height ) ) )
		return;
	ImGui::TableSetupColumn( "list", ImGuiTableColumnFlags_WidthFixed, Ui( 250 ) );
	ImGui::TableSetupColumn( "canvas", ImGuiTableColumnFlags_WidthStretch );
	ImGui::TableSetupColumn( "props", ImGuiTableColumnFlags_WidthFixed, Ui( 330 ) );
	ImGui::TableNextRow();

	ImGui::TableSetColumnIndex( 0 );
	ImGui::BeginChild( "##left", ImVec2( 0, height ) );
	DrawList( app );
	ImGui::EndChild();
	// After the list: it may have changed the screen.
	editor.screen  = std::clamp( editor.screen, 0, (int)setup.screens.size() - 1 );
	Screen& screen = setup.screens[ editor.screen ];

	ImGui::TableSetColumnIndex( 1 );
	ImGui::BeginChild( "##middle", ImVec2( 0, height ) );
	{
		// Both side by side, or one large.
		const char* views[] = { "Both", "Takes from", "Goes to" };
		for( int v = 0; v < 3; ++v )
		{
			if( v > 0 )
				ImGui::SameLine( 0, 0 );
			bool on = (int)editor.view == v;
			if( on )
				ImGui::PushStyleColor( ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4( theme::Edge ) );
			if( ImGui::Button( views[ v ] ) )
			{
				editor.view = (Editor::View)v;
				if( v == 1 )
					editor.tab = InputTab;
				if( v == 2 )
					editor.tab = OutputTab;
			}
			if( on )
				ImGui::PopStyleColor();
		}
		ImGui::SameLine( 0, Ui( 12 ) );
		if( ImGui::Button( "Fit" ) )
			for( int t = 0; t < 2; ++t )
			{
				editor.zoom[ t ] = 1;
				editor.pan[ t ]  = ImVec2( 0, 0 );
			}
		if( ImGui::IsItemHovered() )
			ImGui::SetTooltip( "Both pictures whole again (the wheel zooms, the middle button pans)" );
		ImGui::SameLine();
		ImGui::Checkbox( "Snap", &editor.snap );
		const char* hint = "Same colour, same number: the same slice on both sides";
		float hw         = ImGui::CalcTextSize( hint ).x;
		if( ImGui::GetContentRegionAvail().x > hw + Ui( 20 ) )
		{
			ImGui::SameLine( ImGui::GetWindowContentRegionMax().x - hw );
			ImGui::AlignTextToFramePadding();
			ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( theme::TextDim ), "%s", hint );
		}
	}
	float stripH = Ui( 112 );
	// A pane: its title (what it shows) over its canvas.
	auto pane = [ & ]( int tab, float width ) {
		ImGui::BeginChild( tab == InputTab ? "##inputPane" : "##outputPane", ImVec2( width, -stripH - Ui( 8 ) ) );
		ImDrawList* draw = ImGui::GetWindowDrawList();
		ImVec2 p         = ImGui::GetCursorScreenPos();
		draw->AddText( Fonts().small, Fonts().small->FontSize, p, theme::TextDim, tab == InputTab ? "1  TAKES FROM" : "2  GOES TO" );
		ImGui::Dummy( ImVec2( 1, Fonts().small->FontSize + Ui( 2 ) ) );
		std::string title, size;
		if( tab == InputTab )
		{
			const Canvas* c = app.EditedOutput().FindCanvas( editor.inputSource );
			title           = c ? PictureName( app, editor.inputSource ) + "'s tracks" : "The composition";
			size            = std::to_string( c ? c->width : app.composition.width ) + " x " + std::to_string( c ? c->height : app.composition.height );
		}
		else
		{
			title = std::to_string( editor.screen + 1 ) + "  " + screen.name;
			size  = std::to_string( screen.width ) + " x " + std::to_string( screen.height );
		}
		ImGui::PushFont( Fonts().bold );
		ImGui::TextUnformatted( title.c_str() );
		ImGui::PopFont();
		ImGui::SameLine();
		ImGui::TextDisabled( "%s   %.0f %%", size.c_str(), editor.zoom[ tab ] * 100 );
		DrawCanvas( app, screen, tab );
		ImGui::EndChild();
	};
	if( editor.view == Editor::View::Both )
	{
		float gap  = Ui( 34 );
		float half = ( ImGui::GetContentRegionAvail().x - gap ) / 2;
		pane( InputTab, half );
		ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
		ImGui::SameLine( 0, gap );
		pane( OutputTab, 0 );
		// From one to the other.
		ImDrawList* draw = ImGui::GetWindowDrawList();
		ImVec2 c( b.x + gap / 2, ( a.y + b.y ) / 2 );
		draw->AddLine( ImVec2( c.x - Ui( 11 ), c.y ), ImVec2( c.x + Ui( 9 ), c.y ), theme::Accent, Ui( 2.5f ) );
		draw->AddLine( ImVec2( c.x + Ui( 2 ), c.y - Ui( 7 ) ), ImVec2( c.x + Ui( 10 ), c.y ), theme::Accent, Ui( 2.5f ) );
		draw->AddLine( ImVec2( c.x + Ui( 2 ), c.y + Ui( 7 ) ), ImVec2( c.x + Ui( 10 ), c.y ), theme::Accent, Ui( 2.5f ) );
	}
	else
		pane( editor.view == Editor::View::Input ? InputTab : OutputTab, 0 );

	// Under the canvases: what the screen sends now, the problems found, and the whole path.
	{
		static std::vector< Problem > problems;
		static int checkedAt = -1000, checkedScreen = -1;
		static const AdvancedOutputSetup* checkedSetup = nullptr; // the Grid's or the Timeline's
		bool dragging = editor.drag != Drag::None;
		if( checkedScreen != editor.screen || checkedSetup != &setup || ( !dragging && ImGui::GetFrameCount() - checkedAt > 15 ) )
		{
			problems      = FindProblems( app, setup, editor.screen );
			checkedAt     = ImGui::GetFrameCount();
			checkedScreen = editor.screen;
			checkedSetup  = &setup;
		}
		ImGui::Spacing();
		if( ImGui::BeginTable( "##checks", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersOuter, ImVec2( 0, stripH ) ) )
		{
			ImGui::TableSetupColumn( "live", ImGuiTableColumnFlags_WidthStretch, 1.0f );
			ImGui::TableSetupColumn( "problems", ImGuiTableColumnFlags_WidthStretch, 1.4f );
			ImGui::TableSetupColumn( "path", ImGuiTableColumnFlags_WidthStretch, 1.0f );
			ImGui::TableNextRow( 0, stripH );
			auto caption = [ & ]( const char* text ) {
				ImGui::GetWindowDrawList()->AddText( Fonts().small, Fonts().small->FontSize, ImGui::GetCursorScreenPos(), theme::TextDim, text );
				ImGui::Dummy( ImVec2( 1, Fonts().small->FontSize + Ui( 4 ) ) );
			};
			ImGui::TableSetColumnIndex( 0 );
			caption( "WHAT IT SENDS NOW" );
			{
				float th = stripH - Ui( 30 ), tw = th * screen.width / std::max( 1, screen.height );
				tw       = std::min( tw, ImGui::GetContentRegionAvail().x * 0.55f );
				th       = tw * screen.height / std::max( 1, screen.width );
				ImVec2 p = ImGui::GetCursorScreenPos();
				ImGui::GetWindowDrawList()->AddRectFilled( p, ImVec2( p.x + tw, p.y + th ), IM_COL32( 0, 0, 0, 255 ) );
				if( GLuint texture = app.EditedRenderer().ScreenTexture( (size_t)editor.screen ) )
					ImGui::GetWindowDrawList()->AddImage( TextureId( texture ), p, ImVec2( p.x + tw, p.y + th ), ImVec2( 0, 1 ), ImVec2( 1, 0 ) );
				ImGui::Dummy( ImVec2( tw, th ) );
				ImGui::SameLine();
				ImGui::PushStyleColor( ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4( theme::TextDim ) );
				ImGui::TextWrapped( "Turn on Test pattern to see each slice's number on the LEDs or the projection." );
				ImGui::PopStyleColor();
			}
			ImGui::TableSetColumnIndex( 1 );
			caption( "PROBLEMS FOUND" );
			ImGui::BeginChild( "##problems", ImVec2( 0, stripH - Ui( 26 ) ) );
			if( problems.empty() )
				ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( theme::TextSoft ), "None: every slice takes from inside the picture, and none overlap." );
			for( const Problem& problem : problems )
			{
				ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( problem.serious ? theme::Error : theme::Accent ), problem.serious ? "!" : "\xE2\x80\xA2" );
				ImGui::SameLine();
				ImGui::PushTextWrapPos( 0 );
				ImGui::TextUnformatted( problem.text.c_str() );
				ImGui::PopTextWrapPos();
			}
			ImGui::EndChild();
			ImGui::TableSetColumnIndex( 2 );
			caption( "THE WHOLE PATH" );
			{
				int slices = 0;
				bool own   = false;
				for( const Slice& sl : screen.slices )
					if( !sl.mask )
					{
						slices++;
						own |= !sl.source.empty();
					}
				char path[ 512 ];
				std::snprintf( path, sizeof( path ), "%s %d x %d  \xE2\x86\x92  %d slice%s  \xE2\x86\x92  %s %d x %d  \xE2\x86\x92  %s", own ? "Its pictures," : "Composition",
				               app.composition.width, app.composition.height, slices, slices == 1 ? "" : "s", screen.name.c_str(), screen.width, screen.height,
				               Destination( screen ).c_str() );
				ImGui::PushTextWrapPos( 0 );
				ImGui::TextUnformatted( path );
				ImGui::PopTextWrapPos();
			}
			ImGui::EndTable();
		}
	}
	ImGui::EndChild();

	ImGui::TableSetColumnIndex( 2 );
	ImGui::BeginChild( "##right", ImVec2( 0, height ) );
	if( editor.slice >= 0 && editor.slice < (int)screen.slices.size() )
		DrawSliceProps( app, screen, screen.slices[ editor.slice ] );
	else
		ImGui::TextDisabled( "Select a slice in the list or on a canvas." );
	ImGui::Spacing();
	if( ImGui::CollapsingHeader( "Mouse and keys" ) )
		ImGui::TextDisabled( "Drag a slice to move it, its points to shape it.\n"
		                     "Double-click the outline of a contour or\npolygon to add a point; Del removes it.\n"
		                     "Double-click a contour point: smooth or\nstraight. Alt-drag it: new Bezier handles.\n"
		                     "Alt on a handle: move it alone.\n"
		                     "Move, size, turn: drag the box to scale\n(Alt: from the centre), the knob to turn.\n"
		                     "Shift: one axis, keep proportions, 15 degrees.\nAlt: free input corner. Ctrl: no snapping.\n"
		                     "Arrows: nudge the slice or the selected point.\nWheel: zoom. Middle button or Space: pan.\nRight-click a slice for more." );
	ImGui::EndChild();
	ImGui::EndTable();
}
}

void DrawAdvancedOutputPanel( App& app )
{
	ImGui::SetNextWindowSize( ImVec2( Ui( 1400 ), Ui( 800 ) ), ImGuiCond_FirstUseEver );
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
	AdvancedOutputSetup& setup = app.EditedOutput();
	if( setup.screens.empty() )
		setup = AdvancedOutputSetup::MakeDefault( app.composition.width, app.composition.height );
	editor.screen = std::clamp( editor.screen, 0, (int)setup.screens.size() - 1 );
	if( editor.slice >= (int)setup.screens[ editor.screen ].slices.size() )
		editor.slice = -1;
	if( editor.step == Editor::Step::Stage )
		editor.step = Editor::Step::Map;

	if( app.outputSavePrompt != 0 )
		DrawSaveQuestion( app );
	DrawHeader( app );
	DrawSteps( app, setup );
	editor.screen = std::clamp( editor.screen, 0, (int)setup.screens.size() - 1 );
	if( editor.slice >= (int)setup.screens[ editor.screen ].slices.size() )
		editor.slice = -1;

	float footHeight = ImGui::GetFrameHeightWithSpacing() + Ui( 8 );
	float bodyHeight = std::max( Ui( 100 ), ImGui::GetContentRegionAvail().y - footHeight );
	if( editor.step == Editor::Step::Screens )
		DrawScreensStep( app, setup, bodyHeight );
	else
		DrawMapStep( app, setup, bodyHeight );
	DrawFoot( app );
	DrawOwnDisplayQuestion( setup );
	ImGui::End();
}
}
