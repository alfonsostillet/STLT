#pragma once
#include <memory>
#include <string>
#include <vector>

namespace stltvj
{
// Pixel coordinates, y down: composition pixels for inputs, screen pixels for outputs.
struct Vec2
{
	float x = 0, y = 0;
};

// How a slice's picture is shaped on its screen.
enum class WarpMode
{
	Perspective, // four corners: keystone and projection correction
	Grid,        // a grid of points joined by straight lines
	Curved,      // a grid of points joined by smooth curves: bending onto curved or uneven surfaces
	Contour,     // the outline: points along the four sides, straight or Bézier, the picture stretched to fill it
	Count
};
const char* WarpModeName( WarpMode mode );

// How contour points join: straight lines, or Bézier curves shaped by each point's two handles.
enum class PointMode
{
	Linear,
	Bezier,
	Count
};
const char* PointModeName( PointMode mode );

// A point of a slice's output contour. The handles are offsets from the point: `in` shapes the curve arriving
// from the previous point, `out` the curve leaving towards the next one.
struct ContourPoint
{
	Vec2 p, in, out;
};

// Edge blending for overlapping projectors: each side fades out over a share of the slice.
struct SoftEdge
{
	float left = 0, right = 0, top = 0, bottom = 0; // 0..0.5 of the slice
	float power     = 2.0f;                          // steepness of the ramp
	float luminance = 0.5f;                          // brightness at the middle of the ramp
	float gamma     = 2.2f;                          // projector gamma to compensate
	bool Active() const { return left > 0 || right > 0 || top > 0 || bottom > 0; }
	float Factor( float u, float v ) const;          // the multiplier at slice position u, v (0..1, v down)
};

// An item of a screen: a slice (a rectangle of the composition, warped onto the screen), a polygon (any shape of
// the composition pinned point by point), or a mask (a shape that blacks out what lies below it in the list).
struct Slice
{
	std::string id; // stable (a chase's steps name slices by it); given by AdvancedOutputSetup::EnsureIds
	std::string name = "Slice";
	// What the slice takes its picture from: "" the composition, else the id of one of the setup's canvases. Its
	// input points are in that picture's pixels.
	std::string source;
	bool enabled     = true;
	bool polygon     = false;
	bool mask        = false;

	// Slice: input corners top-left, top-right, bottom-right, bottom-left. Polygon: its points, any number >= 3.
	std::vector< Vec2 > input;

	// Slice output.
	WarpMode warp = WarpMode::Perspective;
	Vec2 corners[ 4 ];         // perspective: top-left, top-right, bottom-right, bottom-left
	int gridColumns = 4, gridRows = 4;
	std::vector< Vec2 > grid;  // grid and curved: gridRows rows of gridColumns points, row by row from the top

	// Contour: the output outline, clockwise from the top-left corner; contourCorners are the indices of the
	// top-left, top-right, bottom-right and bottom-left corners (contourCorners[ 0 ] is always 0).
	// Masks keep their shape here too, any number of points >= 3, without corners.
	std::vector< ContourPoint > contour;
	int contourCorners[ 4 ] = { 0, 1, 2, 3 };
	PointMode pointMode     = PointMode::Bezier;

	// Polygon output: where each input point lands.
	std::vector< Vec2 > output;

	SoftEdge edge;
	bool flipHorizontal = false;
	bool flipVertical   = false;
	float opacity       = 1.0f; // for a mask: how much it darkens

	// Mask: black inside the shape, or outside it when inverted, with an edge this soft (screen pixels).
	bool invert   = false;
	float feather = 0;

	// The output is shaped by contour points: a mask, or a slice with the Contour warp.
	bool HasContour() const;

	// Output position of slice point u, v (0..1, v down).
	Vec2 Map( float u, float v ) const;
	// Output positions of an (n + 1) x (n + 1) lattice over the slice, row by row: Map() for many points at once.
	std::vector< Vec2 > MapGrid( int n ) const;
	// Composition position of slice point u, v.
	Vec2 Source( float u, float v ) const;
	// Rebuilds the grid from the current shape (keeps the look when switching to a grid or resizing it).
	void ResampleGrid( int columns, int rows );
	void SetWarp( WarpMode mode );
	// Output follows the input, scaled from composition to screen size.
	void MatchOutputToInput( float compWidth, float compHeight, float screenWidth, float screenHeight );
	void FitOutput( float screenWidth, float screenHeight );
	// The outline of the output shape, as screen points (for drawing and hit tests).
	std::vector< Vec2 > OutputOutline( int stepsPerSide = 16 ) const;
	std::vector< Vec2 > InputOutline() const;

	// Contour editing. Insert adds a point on the outline nearest to `near` (within maxDistance) without changing
	// the shape, and returns its index or -1. Corners cannot be removed.
	int InsertContourPoint( Vec2 near, float maxDistance );
	bool RemoveContourPoint( int index );
	bool IsContourCorner( int index ) const;
	// Gives a point smooth handles from its neighbours; corners get straight ones along their two sides.
	void SmoothContourPoint( int index );
	// A plain four-corner contour on the current outer corners.
	void ResetContour();
	// Polygons: a new point on the edge nearest to `near`, in the input (onOutput false) or output outline;
	// the other outline gets the matching point. Returns its index or -1.
	int InsertPolygonPoint( bool onOutput, Vec2 near, float maxDistance );
	bool RemovePolygonPoint( int index );
};

enum class OutputDevice
{
	Virtual, // rendered but not shown: for previews and recording
	Window,  // a movable window
	Display, // fullscreen on a display, chosen by name
	Count
};
const char* OutputDeviceName( OutputDevice device );

struct Screen
{
	std::string name = "Screen";
	bool enabled     = true;
	OutputDevice device = OutputDevice::Virtual;
	std::string display; // monitor name for Display
	int width = 1920, height = 1080;
	float opacity    = 1.0f;
	float brightness = 0, contrast = 0; // -1..1
	float red = 0, green = 0, blue = 0; // -1..1
	bool testPattern = false;           // draws each slice as a numbered grid, for lining up projectors
	bool share       = false;           // also to other programs on this computer: Spout (Windows) or Syphon (macOS)
	bool ndi         = false;           // also over the network as an NDI source
	std::vector< Slice > slices;
};

// A picture of its own besides the composition, with its own size: the timeline's tracks can be sent to it
// instead of the composition (a LED wall, side screens), and slices take their picture from it.
struct Canvas
{
	std::string id; // what tracks and slices name it by; given by AdvancedOutputSetup::EnsureIds
	std::string name = "Canvas";
	int width = 1920, height = 1080;
};

struct AdvancedOutputSetup
{
	std::vector< Screen > screens;
	std::vector< Canvas > canvases;
	const Canvas* FindCanvas( const std::string& id ) const;
	// The size of what a slice with this source takes from: the canvas, or the composition ("" or a canvas gone).
	void SourceSize( const std::string& source, int compWidth, int compHeight, int& width, int& height ) const;

	// One screen with one slice showing the whole composition.
	static AdvancedOutputSetup MakeDefault( int compWidth, int compHeight );
	static Slice MakeSlice( const std::string& name, float compWidth, float compHeight, float screenWidth, float screenHeight );
	static Slice MakePolygon( const std::string& name, float compWidth, float compHeight, float screenWidth, float screenHeight );
	// A regular shape of `sides` points (3: triangle, 4: square, many: a circle) in the middle of the composition.
	static Slice MakeRegularPolygon( const std::string& name, int sides, float compWidth, float compHeight, float screenWidth, float screenHeight );
	// A polygon through points drawn by hand: in composition pixels (the input), or in screen pixels (the output,
	// `onOutput`); the other side follows, scaled between composition and screen.
	static Slice MakePolygonFrom( const std::string& name, const std::vector< Vec2 >& points, bool onOutput, float compWidth, float compHeight,
	                              float screenWidth, float screenHeight );
	// A rectangle or an ellipse in the middle of the screen.
	static Slice MakeMask( const std::string& name, float screenWidth, float screenHeight, bool ellipse );
	// Gives every slice and canvas an id, and a new one to one sharing another's (a duplicate); true if any changed.
	bool EnsureIds();
	// A new canvas with a fresh id (not added).
	Canvas MakeCanvas( const std::string& name, int width, int height ) const;
	// A canvas for screen `index`, its size and name; the screen's slices that showed the whole composition show
	// the whole canvas instead (the screen now shows what goes to the canvas). Returns the canvas's id.
	std::string AddScreenCanvas( size_t index, int compWidth, int compHeight );
	// Removes a canvas; its slices go back to the composition.
	void RemoveCanvas( const std::string& id, int compWidth, int compHeight );
	// Changes what a slice takes from, its input moved and scaled from the old picture's size to the new one's.
	void SetSliceSource( Slice& slice, const std::string& source, int compWidth, int compHeight ) const;
};

std::string SaveAdvancedOutput( const AdvancedOutputSetup& setup );
// The stage for the STLT-CHASER editor (chaser-stage.json): the composition size, the screens and each slice's
// id, name, input (composition pixels) and output (screen pixels); masks left out. Ids must be set (EnsureIds).
std::string SaveChaserStage( const AdvancedOutputSetup& setup, int compWidth, int compHeight );
bool LoadAdvancedOutput( const std::string& json, AdvancedOutputSetup& setup, std::string& error );
// Reads Resolume Arena/Avenue's AdvancedOutput.xml: screens, slices (input and output rectangles) and polygons.
bool ImportResolumeXml( const std::string& xml, AdvancedOutputSetup& setup, std::string& error, int* compWidth = nullptr, int* compHeight = nullptr );

// Geometry helpers.
// Splits a simple polygon into triangles (indices into `points`, three per triangle).
std::vector< int > Triangulate( const std::vector< Vec2 >& points );
bool PointInPolygon( const std::vector< Vec2 >& polygon, Vec2 p );
}
