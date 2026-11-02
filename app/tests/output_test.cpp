// Tests for the advanced output without a GPU: warps, contours, masks, polygons, soft edges, saving and Resolume import.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include "output/AdvancedOutput.h"
#include "render/OutputWindow.h"
#include <json.hpp>

using namespace stltvj;

static int failures = 0;

#define CHECK( condition )                                                                           \
	do                                                                                               \
	{                                                                                                \
		if( !( condition ) )                                                                         \
		{                                                                                            \
			std::fprintf( stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition ); \
			++failures;                                                                              \
		}                                                                                            \
	} while( 0 )

static bool Near( float a, float b, float tolerance )
{
	return std::fabs( a - b ) < tolerance;
}

static bool Near( Vec2 a, Vec2 b, float tolerance = 0.01f )
{
	return std::fabs( a.x - b.x ) < tolerance && std::fabs( a.y - b.y ) < tolerance;
}

static void TestPerspective()
{
	Slice s = AdvancedOutputSetup::MakeSlice( "A", 1000, 500, 1920, 1080 );
	CHECK( Near( s.Map( 0, 0 ), { 0, 0 } ) );
	CHECK( Near( s.Map( 1, 1 ), { 1920, 1080 } ) );
	CHECK( Near( s.Map( 0.5f, 0.5f ), { 960, 540 } ) );
	CHECK( Near( s.Source( 0.5f, 0.25f ), { 500, 125 } ) );

	// A keystoned quad: the corners land exactly, the middle follows the projection (not the average of corners).
	s.corners[ 0 ] = { 100, 0 };
	s.corners[ 1 ] = { 300, 0 };
	s.corners[ 2 ] = { 400, 100 };
	s.corners[ 3 ] = { 0, 100 };
	CHECK( Near( s.Map( 1, 0 ), { 300, 0 } ) );
	CHECK( Near( s.Map( 0, 1 ), { 0, 100 } ) );
	Vec2 middle = s.Map( 0.5f, 0.5f );
	CHECK( std::fabs( middle.x - 200 ) < 0.01f );
	CHECK( std::fabs( middle.y - 100.0f / 3 ) < 0.05f ); // the centre sits nearer the far (narrow) edge, 1:2 here

	// Flips mirror the source, not the output.
	s.flipHorizontal = true;
	CHECK( Near( s.Source( 0, 0 ), { 1000, 0 } ) );
	CHECK( Near( s.Map( 0, 0 ), { 100, 0 } ) );
}

static void TestGrids()
{
	Slice s = AdvancedOutputSetup::MakeSlice( "A", 100, 100, 400, 200 );
	s.SetWarp( WarpMode::Curved );
	CHECK( s.gridColumns == 4 && s.gridRows == 4 && s.grid.size() == 16 );
	// A flat grid reproduces the rectangle everywhere, curved or not.
	for( float u : { 0.0f, 0.3f, 0.5f, 1.0f } )
		for( float v : { 0.0f, 0.7f, 1.0f } )
			CHECK( Near( s.Map( u, v ), { 400 * u, 200 * v }, 0.05f ) );
	// Lifting one inner point bends the surface smoothly around it but leaves the corners.
	s.grid[ 1 * 4 + 1 ].y -= 30;
	CHECK( Near( s.Map( 0, 0 ), { 0, 0 } ) );
	CHECK( Near( s.Map( 1, 1 ), { 400, 200 } ) );
	Vec2 at = s.Map( 1.0f / 3, 1.0f / 3 );
	CHECK( Near( at, { 133.333f, 66.667f - 30 }, 0.05f ) );
	Vec2 between = s.Map( 0.5f, 1.0f / 3 );
	CHECK( between.y < 66.667f - 1 && between.y > 66.667f - 30 );
	// The straight grid joins points with lines: halfway between two points is their average.
	s.warp = WarpMode::Grid;
	Vec2 a = s.grid[ 1 * 4 + 1 ], b = s.grid[ 1 * 4 + 2 ];
	CHECK( Near( s.Map( 0.5f, 1.0f / 3 ), { ( a.x + b.x ) / 2, ( a.y + b.y ) / 2 }, 0.05f ) );
	// Resampling keeps the shape; back to perspective keeps the corners.
	s.ResampleGrid( 6, 3 );
	CHECK( s.grid.size() == 18 );
	CHECK( Near( s.Map( 1, 1 ), { 400, 200 }, 0.05f ) );
	s.corners[ 2 ] = { 0, 0 };
	s.SetWarp( WarpMode::Perspective );
	CHECK( Near( s.corners[ 2 ], { 400, 200 }, 0.05f ) );
}

static void TestPolygons()
{
	std::vector< Vec2 > square = { { 0, 0 }, { 10, 0 }, { 10, 10 }, { 0, 10 } };
	CHECK( Triangulate( square ).size() == 6 );
	// A concave L shape: four triangles, none outside the shape.
	std::vector< Vec2 > l = { { 0, 0 }, { 20, 0 }, { 20, 10 }, { 10, 10 }, { 10, 20 }, { 0, 20 } };
	std::vector< int > t  = Triangulate( l );
	CHECK( t.size() == 12 );
	for( size_t i = 0; i + 2 < t.size(); i += 3 )
	{
		Vec2 c{ ( l[ t[ i ] ].x + l[ t[ i + 1 ] ].x + l[ t[ i + 2 ] ].x ) / 3, ( l[ t[ i ] ].y + l[ t[ i + 1 ] ].y + l[ t[ i + 2 ] ].y ) / 3 };
		CHECK( PointInPolygon( l, c ) );
	}
	CHECK( PointInPolygon( l, { 5, 15 } ) );
	CHECK( !PointInPolygon( l, { 15, 15 } ) );
}

static void TestSoftEdge()
{
	SoftEdge e;
	CHECK( std::fabs( e.Factor( 0.5f, 0.5f ) - 1 ) < 1e-6 );
	e.left = 0.2f;
	CHECK( e.Factor( 0.0f, 0.5f ) < 1e-6 );
	CHECK( std::fabs( e.Factor( 0.3f, 0.5f ) - 1 ) < 1e-6 );
	float middle = e.Factor( 0.1f, 0.5f ); // luminance 0.5 through gamma 2.2
	CHECK( std::fabs( middle - std::pow( 0.5f, 1 / 2.2f ) ) < 1e-4 );
	CHECK( e.Factor( 0.05f, 0.5f ) < middle && e.Factor( 0.15f, 0.5f ) > middle );
}

static void TestSaveLoad()
{
	AdvancedOutputSetup setup = AdvancedOutputSetup::MakeDefault( 1920, 1080 );
	Screen& screen            = setup.screens[ 0 ];
	screen.device             = OutputDevice::Display;
	screen.display            = "Projector";
	screen.brightness         = 0.1f;
	Slice& slice              = screen.slices[ 0 ];
	slice.SetWarp( WarpMode::Curved );
	slice.grid[ 5 ].x += 12;
	slice.edge.right = 0.15f;
	slice.flipVertical = true;
	Vec2 moved         = slice.grid[ 5 ];
	screen.slices.push_back( AdvancedOutputSetup::MakePolygon( "P", 1920, 1080, 1920, 1080 ) ); // `slice` is stale from here

	AdvancedOutputSetup loaded;
	std::string error;
	CHECK( LoadAdvancedOutput( SaveAdvancedOutput( setup ), loaded, error ) );
	CHECK( loaded.screens.size() == 1 );
	const Screen& s = loaded.screens[ 0 ];
	CHECK( s.device == OutputDevice::Display && s.display == "Projector" && std::fabs( s.brightness - 0.1f ) < 1e-6 );
	CHECK( s.slices.size() == 2 );
	CHECK( s.slices[ 0 ].warp == WarpMode::Curved && s.slices[ 0 ].grid.size() == 16 );
	CHECK( Near( s.slices[ 0 ].grid[ 5 ], moved ) );
	CHECK( std::fabs( s.slices[ 0 ].edge.right - 0.15f ) < 1e-6 && s.slices[ 0 ].flipVertical );
	CHECK( s.slices[ 1 ].polygon && s.slices[ 1 ].output.size() == 3 );
	CHECK( !LoadAdvancedOutput( "{}", loaded, error ) );
}

static void TestResolumeImport()
{
	// The shape Arena writes (as in editor/test/model.test.mjs).
	const char* xml = R"(<?xml version="1.0" encoding="utf-8"?>
<ScreenSetup name="ScreenSetup">
  <CurrentCompositionTextureSize width="1000" height="500"/>
  <screens>
    <Screen name="Screen 1" uniqueId="10">
      <Params name="Params"><Param name="Name" T="STRING" default="" value="Main &amp; Side"/></Params>
      <layers>
        <Slice uniqueId="2"><Params name="Common"><Param name="Name" T="STRING" default="Layer" value="B"/></Params>
          <InputRect orientation="0"><v x="500" y="0"/><v x="1000" y="0"/><v x="1000" y="500"/><v x="500" y="500"/></InputRect>
          <OutputRect orientation="0"><v x="0" y="0"/><v x="10" y="0"/><v x="10" y="10"/><v x="0" y="10"/></OutputRect>
        </Slice>
        <Slice uniqueId="1"><Params name="Common"><Param name="Name" T="STRING" default="Layer" value="A"/></Params>
          <InputRect orientation="0"><v x="0" y="0"/><v x="500" y="0"/><v x="500" y="500"/><v x="0" y="500"/></InputRect>
        </Slice>
        <Polygon uniqueId="3"><Params name="Common"><Param name="Name" T="STRING" default="Layer" value="P"/></Params>
          <InputContour><v x="0" y="0"/><v x="10" y="0"/><v x="5" y="10"/></InputContour>
        </Polygon>
      </layers>
      <OutputDevice><OutputDeviceVirtual name="Screen 1" width="1920" height="1080"/></OutputDevice>
    </Screen>
  </screens>
</ScreenSetup>)";
	AdvancedOutputSetup setup;
	std::string error;
	int cw = 0, ch = 0;
	CHECK( ImportResolumeXml( xml, setup, error, &cw, &ch ) );
	CHECK( cw == 1000 && ch == 500 );
	CHECK( setup.screens.size() == 1 );
	const Screen& s = setup.screens[ 0 ];
	CHECK( s.name == "Main & Side" && s.width == 1920 && s.height == 1080 && s.device == OutputDevice::Virtual );
	CHECK( s.slices.size() == 3 );
	// Oldest first: the polygon, then A, then B (Arena writes the newest first).
	CHECK( s.slices[ 0 ].name == "P" && s.slices[ 0 ].polygon && s.slices[ 0 ].output.size() == 3 );
	CHECK( s.slices[ 1 ].name == "A" && Near( s.slices[ 1 ].corners[ 2 ], { 960, 1080 } ) ); // no output rect: follows the input
	CHECK( s.slices[ 2 ].name == "B" && Near( s.slices[ 2 ].input[ 0 ], { 500, 0 } ) && Near( s.slices[ 2 ].corners[ 2 ], { 10, 10 } ) );
	CHECK( !ImportResolumeXml( "<nope/>", setup, error ) );
}

static void TestContour()
{
	// A plain four-corner contour is exactly the perspective warp.
	Slice persp = AdvancedOutputSetup::MakeSlice( "A", 100, 100, 400, 200 );
	persp.corners[ 0 ] = { 40, 10 };
	persp.corners[ 2 ] = { 380, 190 };
	Slice s = persp;
	s.SetWarp( WarpMode::Contour );
	CHECK( s.contour.size() == 4 && s.contourCorners[ 3 ] == 3 && s.pointMode == PointMode::Bezier );
	for( float u : { 0.0f, 0.25f, 0.5f, 0.9f } )
		for( float v : { 0.0f, 0.4f, 1.0f } )
			CHECK( Near( s.Map( u, v ), persp.Map( u, v ), 0.05f ) );
	std::vector< Vec2 > lattice = s.MapGrid( 4 );
	CHECK( lattice.size() == 25 && Near( lattice[ 2 * 5 + 1 ], s.Map( 0.25f, 0.5f ), 0.01f ) );

	// A point added on the top side leaves the shape alone; pulling it up bends the top, fading towards the bottom.
	s = AdvancedOutputSetup::MakeSlice( "B", 100, 100, 400, 200 );
	s.SetWarp( WarpMode::Contour );
	CHECK( s.InsertContourPoint( { 210, 30 }, 5 ) == -1 ); // too far from the outline
	int top = s.InsertContourPoint( { 200, 2 }, 5 );
	CHECK( top == 1 && s.contour.size() == 5 );
	CHECK( s.contourCorners[ 1 ] == 2 && s.contourCorners[ 2 ] == 3 && s.contourCorners[ 3 ] == 4 );
	CHECK( Near( s.contour[ top ].p, { 200, 0 }, 0.5f ) );
	CHECK( Near( s.Map( 0.3f, 0.6f ), { 120, 120 }, 0.2f ) );
	s.contour[ top ].p.y -= 40;
	CHECK( Near( s.Map( 0, 0 ), { 0, 0 } ) && Near( s.Map( 1, 1 ), { 400, 200 } ) );
	CHECK( Near( s.Map( 0.5f, 0 ), { 200, -40 }, 0.5f ) );
	CHECK( Near( s.Map( 0.5f, 0.5f ), { 200, 80 }, 0.5f ) );
	CHECK( Near( s.Map( 0.5f, 1 ), { 200, 200 }, 0.01f ) );
	// Bézier: the point's handles are level, so the side leaves the corner flat and rises smoothly; linear: two
	// straight segments.
	CHECK( s.Map( 0.125f, 0 ).y > -8 );
	s.pointMode = PointMode::Linear;
	CHECK( Near( s.Map( 0.25f, 0 ), { 100, -20 }, 0.5f ) );
	CHECK( Near( s.Map( 0.125f, 0 ), { 50, -10 }, 0.5f ) );
	std::vector< Vec2 > outline = s.OutputOutline();
	CHECK( outline.size() == 5 && Near( outline[ 1 ], { 200, -40 } ) );

	// Saved and loaded as it is.
	AdvancedOutputSetup setup = AdvancedOutputSetup::MakeDefault( 100, 100 );
	setup.screens[ 0 ].slices[ 0 ] = s;
	AdvancedOutputSetup loaded;
	std::string error;
	CHECK( LoadAdvancedOutput( SaveAdvancedOutput( setup ), loaded, error ) );
	const Slice& l = loaded.screens[ 0 ].slices[ 0 ];
	CHECK( l.warp == WarpMode::Contour && l.pointMode == PointMode::Linear && l.contour.size() == 5 && l.contourCorners[ 1 ] == 2 );
	CHECK( Near( l.contour[ 1 ].p, s.contour[ 1 ].p ) && Near( l.contour[ 1 ].out, s.contour[ 1 ].out ) );
	CHECK( Near( l.Map( 0.3f, 0.7f ), s.Map( 0.3f, 0.7f ) ) );

	// Corners stay; other points go.
	CHECK( !s.RemoveContourPoint( 0 ) && !s.RemoveContourPoint( 2 ) );
	CHECK( s.RemoveContourPoint( 1 ) && s.contour.size() == 4 && s.contourCorners[ 3 ] == 3 );
	// Match and fit keep the contour mode.
	s.FitOutput( 800, 600 );
	CHECK( s.warp == WarpMode::Contour && Near( s.Map( 1, 1 ), { 800, 600 } ) );

	// From a grid, the contour takes the grid's border points.
	Slice g = AdvancedOutputSetup::MakeSlice( "C", 100, 100, 400, 200 );
	g.SetWarp( WarpMode::Curved );
	g.SetWarp( WarpMode::Contour );
	CHECK( g.contour.size() == 12 && g.contourCorners[ 1 ] == 3 && g.contourCorners[ 2 ] == 6 && g.contourCorners[ 3 ] == 9 );
	CHECK( Near( g.Map( 0.5f, 0.5f ), { 200, 100 }, 0.5f ) );

	// Polygons: a point added on an output edge gets its twin on the matching input edge.
	Slice p    = AdvancedOutputSetup::MakePolygon( "P", 100, 100, 200, 200 );
	Vec2 a = p.output[ 0 ], b = p.output[ 1 ], ia = p.input[ 0 ], ib = p.input[ 1 ];
	int at     = p.InsertPolygonPoint( true, { ( a.x + b.x ) / 2, ( a.y + b.y ) / 2 }, 2 );
	CHECK( at == 1 && p.output.size() == 4 && p.input.size() == 4 );
	CHECK( Near( p.input[ 1 ], { ( ia.x + ib.x ) / 2, ( ia.y + ib.y ) / 2 }, 0.05f ) );
	CHECK( p.RemovePolygonPoint( 1 ) && p.output.size() == 3 && !p.RemovePolygonPoint( 0 ) );
}

static void TestMasks()
{
	// A rectangle mask: its outline is the rectangle, straight.
	Slice r = AdvancedOutputSetup::MakeMask( "M", 800, 400, false );
	CHECK( r.mask && r.HasContour() && r.contour.size() == 4 && r.InputOutline().empty() );
	std::vector< Vec2 > outline = r.OutputOutline( 4 );
	CHECK( outline.size() == 16 && Near( outline[ 0 ], { 200, 100 } ) && Near( outline[ 2 ], { 400, 100 }, 0.05f ) );
	// An ellipse: every outline point lies on it.
	Slice e = AdvancedOutputSetup::MakeMask( "E", 800, 400, true );
	for( Vec2 p : e.OutputOutline( 16 ) )
	{
		float dx = ( p.x - 400 ) / 200, dy = ( p.y - 200 ) / 100;
		CHECK( std::fabs( std::sqrt( dx * dx + dy * dy ) - 1 ) < 0.002f );
	}
	// Points come and go, down to a triangle; no corners are kept.
	int at = r.InsertContourPoint( { 300, 101 }, 4 );
	CHECK( at == 1 && r.contour.size() == 5 && !r.IsContourCorner( 0 ) );
	CHECK( r.RemoveContourPoint( 0 ) && r.RemoveContourPoint( 0 ) && r.contour.size() == 3 && !r.RemoveContourPoint( 0 ) );
	// Fit stretches the shape over the screen.
	e.FitOutput( 1000, 500 );
	float minX = 1e9f, maxX = -1e9f, minY = 1e9f, maxY = -1e9f;
	for( Vec2 p : e.OutputOutline( 32 ) )
	{
		minX = std::min( minX, p.x );
		maxX = std::max( maxX, p.x );
		minY = std::min( minY, p.y );
		maxY = std::max( maxY, p.y );
	}
	CHECK( std::fabs( minX ) < 0.5f && std::fabs( maxX - 1000 ) < 0.5f && std::fabs( minY ) < 0.5f && std::fabs( maxY - 500 ) < 0.5f );
	// Matching the input leaves a mask alone (it has none).
	Vec2 before = e.contour[ 0 ].p;
	e.MatchOutputToInput( 100, 100, 200, 200 );
	CHECK( Near( e.contour[ 0 ].p, before ) );

	// Saved and loaded with its settings.
	AdvancedOutputSetup setup = AdvancedOutputSetup::MakeDefault( 800, 400 );
	e.invert                  = true;
	e.feather                 = 25;
	e.opacity                 = 0.75f;
	setup.screens[ 0 ].slices.push_back( e );
	AdvancedOutputSetup loaded;
	std::string error;
	CHECK( LoadAdvancedOutput( SaveAdvancedOutput( setup ), loaded, error ) );
	CHECK( loaded.screens[ 0 ].slices.size() == 2 );
	const Slice& m = loaded.screens[ 0 ].slices[ 1 ];
	CHECK( m.mask && m.invert && std::fabs( m.feather - 25 ) < 1e-6 && std::fabs( m.opacity - 0.75f ) < 1e-6 && m.contour.size() == 4 );
	CHECK( Near( m.contour[ 1 ].in, e.contour[ 1 ].in ) );
}

static void TestResolumeWarpImport()
{
	// Warpers, a soft edge and a mask, in the shapes Resolume's files use (the importer accepts the variants).
	std::string grid3;
	for( int r = 0; r < 3; ++r )
		for( int c = 0; c < 3; ++c )
		{
			float x = c / 2.0f, y = r / 2.0f;
			if( r == 1 && c == 1 )
				y -= 0.1f; // the middle point raised a tenth
			grid3 += "<v x=\"" + std::to_string( x ) + "\" y=\"" + std::to_string( y ) + "\"/>";
		}
	std::string flat3;
	for( int r = 0; r < 3; ++r )
		for( int c = 0; c < 3; ++c )
			flat3 += "<v x=\"" + std::to_string( c / 2.0f ) + "\" y=\"" + std::to_string( r / 2.0f ) + "\"/>";
	// Bezier, in pixels: 2 x 2 anchors with handles, 4 x 4 points, the top right corner pulled out.
	std::string bezier;
	for( int r = 0; r < 4; ++r )
		for( int c = 0; c < 4; ++c )
		{
			float x = c * 100.0f, y = r * 100.0f;
			if( r == 0 && c == 3 )
				x += 50;
			bezier += "<v x=\"" + std::to_string( x ) + "\" y=\"" + std::to_string( y ) + "\"/>";
		}
	std::string xml = R"(<ScreenSetup><CurrentCompositionTextureSize width="1000" height="500"/><screens>
<Screen name="S"><layers>
<Slice uniqueId="4"><Params name="Common"><Param name="Name" value="Bez"/></Params>
  <InputRect><v x="0" y="0"/><v x="1000" y="0"/><v x="1000" y="500"/><v x="0" y="500"/></InputRect>
  <OutputRect><v x="0" y="0"/><v x="300" y="0"/><v x="300" y="300"/><v x="0" y="300"/></OutputRect>
  <Warper><BezierWarper controlWidth="2" controlHeight="2"><vertices>)" + bezier + R"(</vertices></BezierWarper></Warper>
</Slice>
<Slice uniqueId="3"><Params name="Common"><Param name="Name" value="Flat"/></Params>
  <InputRect><v x="0" y="0"/><v x="1000" y="0"/><v x="1000" y="500"/><v x="0" y="500"/></InputRect>
  <OutputRect><v x="0" y="0"/><v x="400" y="0"/><v x="400" y="200"/><v x="0" y="200"/></OutputRect>
  <Warper><BezierWarper controlWidth="3" controlHeight="3"><vertices>)" + flat3 + R"(</vertices></BezierWarper></Warper>
</Slice>
<Slice uniqueId="2"><Params name="Common"><Param name="Name" value="Warped"/></Params>
  <InputRect><v x="0" y="0"/><v x="1000" y="0"/><v x="1000" y="500"/><v x="0" y="500"/></InputRect>
  <OutputRect><v x="0" y="0"/><v x="400" y="0"/><v x="400" y="200"/><v x="0" y="200"/></OutputRect>
  <Warper><Params name="Warper"><ParamChoice name="Point Mode" value="PM_LINEAR"/></Params>
    <BezierWarper controlWidth="3" controlHeight="3"><vertices>)" + grid3 + R"(</vertices></BezierWarper></Warper>
  <Params name="Soft Edge"><ParamRange name="Left" value="0.2"/><ParamRange name="Right" value="40"/><ParamRange name="Gamma" value="1.8"/></Params>
</Slice>
<Mask uniqueId="1"><Params name="Common"><Param name="Name" value="Door"/></Params>
  <Params name="Mask"><Param name="Invert" value="1"/></Params>
  <OutputContour><v x="10" y="10"/><v x="90" y="10"/><v x="50" y="90"/></OutputContour>
</Mask>
</layers><OutputDevice><OutputDeviceVirtual width="800" height="600"/></OutputDevice></Screen></screens></ScreenSetup>)";
	AdvancedOutputSetup setup;
	std::string error;
	CHECK( ImportResolumeXml( xml, setup, error ) );
	const std::vector< Slice >& items = setup.screens[ 0 ].slices;
	CHECK( items.size() == 4 );
	if( items.size() != 4 )
		return;
	// Oldest first: the mask, then Warped, Flat, Bez.
	const Slice& mask = items[ 0 ];
	CHECK( mask.mask && mask.name == "Door" && mask.invert && mask.contour.size() == 3 && Near( mask.contour[ 2 ].p, { 50, 90 } ) );
	const Slice& warped = items[ 1 ];
	CHECK( warped.warp == WarpMode::Grid && warped.gridColumns == 3 && warped.gridRows == 3 );
	CHECK( Near( warped.Map( 0.5f, 0.5f ), { 200, 80 }, 0.05f ) ); // raised a tenth of 200
	CHECK( Near( warped.Map( 1, 1 ), { 400, 200 }, 0.05f ) );
	CHECK( std::fabs( warped.edge.left - 0.2f ) < 1e-5f && std::fabs( warped.edge.right - 0.1f ) < 1e-5f && std::fabs( warped.edge.gamma - 1.8f ) < 1e-5f );
	CHECK( items[ 2 ].warp == WarpMode::Perspective && !items[ 2 ].edge.Active() ); // an untouched warper
	const Slice& bez = items[ 3 ];
	CHECK( bez.warp == WarpMode::Curved && bez.gridColumns == 2 && bez.gridRows == 2 && Near( bez.Map( 1, 0 ), { 350, 0 }, 0.05f ) );
}

// Displays sharing a name (Windows calls most of them "Generic PnP Monitor") are told apart: the one chosen is the
// one found again, also when they are listed in another order or one moves; ids from before (a name) still work.
static void TestDisplayIds()
{
	using Display = OutputWindow::Display;
	auto make     = []( const char* name, int x, int y ) {
		Display d;
		d.monitorName = name;
		d.x           = x;
		d.y           = y;
		return d;
	};
	std::vector< Display > displays = { make( "Generic PnP Monitor", 0, 0 ), make( "Generic PnP Monitor", 1920, 0 ), make( "DELL U2720Q", -2560, 0 ) };
	OutputWindow::SetIds( displays );
	CHECK( displays[ 0 ].id != displays[ 1 ].id );
	CHECK( OutputWindow::MatchDisplay( displays, displays[ 1 ].id ) == 1 );
	CHECK( OutputWindow::MatchDisplay( displays, displays[ 0 ].id ) == 0 );
	CHECK( OutputWindow::MatchDisplay( displays, displays[ 2 ].id ) == 2 );
	// Listed the other way round: still the one at 1920,0.
	std::string second              = displays[ 1 ].id;
	std::vector< Display > reversed = { displays[ 1 ], displays[ 0 ], displays[ 2 ] };
	OutputWindow::SetIds( reversed );
	CHECK( OutputWindow::MatchDisplay( reversed, second ) == 0 );
	// Moved on the desktop: the second of that name.
	std::vector< Display > moved = { make( "Generic PnP Monitor", 0, 0 ), make( "Generic PnP Monitor", 0, 1080 ) };
	OutputWindow::SetIds( moved );
	CHECK( OutputWindow::MatchDisplay( moved, second ) == 1 );
	// A name alone (setups saved before): the first of that name; unknown: none.
	CHECK( OutputWindow::MatchDisplay( displays, "DELL U2720Q" ) == 2 );
	CHECK( OutputWindow::MatchDisplay( displays, "Missing Monitor#1@0,0" ) == -1 );
}

// Slice ids (a chase's steps name slices by them) and the stage written for the STLT-CHASER editor.
static void TestChaserStage()
{
	AdvancedOutputSetup setup = AdvancedOutputSetup::MakeDefault( 400, 100 );
	Screen& screen            = setup.screens[ 0 ];
	screen.slices.clear();
	for( int i = 0; i < 4; ++i )
	{
		Slice s = AdvancedOutputSetup::MakeSlice( "S" + std::to_string( i ), 400, 100, 400, 100 );
		s.input = { { i * 100.0f, 0 }, { i * 100.0f + 100, 0 }, { i * 100.0f + 100, 100 }, { i * 100.0f, 100 } };
		screen.slices.push_back( s );
	}
	screen.slices.push_back( screen.slices[ 3 ] ); // a duplicate: its id must change
	CHECK( setup.EnsureIds() && screen.slices[ 4 ].id != screen.slices[ 3 ].id && !setup.EnsureIds() );
	screen.slices.pop_back();
	screen.slices.push_back( AdvancedOutputSetup::MakeMask( "Mask", 400, 100, false ) );
	setup.EnsureIds();

	nlohmann::json stage = nlohmann::json::parse( SaveChaserStage( setup, 400, 100 ) );
	CHECK( stage[ "format" ] == "stlt-vj-chaser-stage" && stage[ "compWidth" ] == 400 && stage[ "compHeight" ] == 100 );
	CHECK( stage[ "screens" ].size() == 1 && stage[ "screens" ][ 0 ][ "width" ] == screen.width );
	CHECK( stage[ "slices" ].size() == 4 ); // the mask is left out
	CHECK( stage[ "slices" ][ 2 ][ "id" ] == screen.slices[ 2 ].id && stage[ "slices" ][ 2 ][ "name" ] == "S2" );
	CHECK( stage[ "slices" ][ 2 ][ "points" ][ 1 ][ 0 ] == 300.0 && stage[ "slices" ][ 2 ][ "outPoints" ].size() == 4 );
	CHECK( stage[ "slices" ][ 0 ][ "screen" ] == stage[ "screens" ][ 0 ][ "id" ] );

	// Ids survive the output setup's saving.
	AdvancedOutputSetup back;
	std::string error;
	CHECK( LoadAdvancedOutput( SaveAdvancedOutput( setup ), back, error ) && back.screens[ 0 ].slices[ 2 ].id == screen.slices[ 2 ].id );
}

// Polygons of any shape: regular ones, drawn ones (on either side), and concave ones cut into triangles that cover
// exactly their area.
static void TestPolygonShapes()
{
	Slice square = AdvancedOutputSetup::MakeRegularPolygon( "S", 4, 1920, 1080, 3840, 2160 );
	CHECK( square.polygon && square.input.size() == 4 && square.output.size() == 4 );
	CHECK( Near( square.input[ 0 ].y, square.input[ 1 ].y, 0.01f ) && Near( square.input[ 1 ].x, square.input[ 2 ].x, 0.01f ) ); // standing straight
	CHECK( Near( square.output[ 2 ].x, square.input[ 2 ].x * 2, 0.01f ) ); // the screen is twice the composition
	Slice circle = AdvancedOutputSetup::MakeRegularPolygon( "C", 32, 1920, 1080, 1920, 1080 );
	CHECK( circle.input.size() == 32 );
	for( Vec2 p : circle.input )
		CHECK( Near( std::hypot( p.x - 960, p.y - 540 ), 324, 0.05f ) );
	CHECK( AdvancedOutputSetup::MakeRegularPolygon( "T", 1, 100, 100, 100, 100 ).input.size() == 3 );

	// An L drawn on the output of a 1920x1080 screen over a 960x540 composition.
	std::vector< Vec2 > l = { { 100, 100 }, { 500, 100 }, { 500, 300 }, { 300, 300 }, { 300, 700 }, { 100, 700 } };
	Slice drawn           = AdvancedOutputSetup::MakePolygonFrom( "L", l, true, 960, 540, 1920, 1080 );
	CHECK( drawn.polygon && drawn.output.size() == 6 && drawn.input.size() == 6 );
	CHECK( Near( drawn.input[ 4 ].x, 150, 0.01f ) && Near( drawn.input[ 4 ].y, 350, 0.01f ) );
	Slice onInput = AdvancedOutputSetup::MakePolygonFrom( "L", l, false, 960, 540, 1920, 1080 );
	CHECK( Near( onInput.output[ 4 ].x, 600, 0.01f ) && Near( onInput.input[ 4 ].x, 300, 0.01f ) );

	// Concave shapes (the L, a five-pointed star) cut into n - 2 triangles whose areas add up to the shape's.
	auto area = []( const std::vector< Vec2 >& p ) {
		double a = 0;
		for( size_t i = 0; i < p.size(); ++i )
		{
			const Vec2 &u = p[ i ], &v = p[ ( i + 1 ) % p.size() ];
			a += (double)u.x * v.y - (double)v.x * u.y;
		}
		return std::fabs( a ) / 2;
	};
	std::vector< Vec2 > star;
	for( int i = 0; i < 10; ++i )
	{
		float r = i % 2 ? 40.0f : 100.0f, a = -1.5707963f + i * 0.62831853f;
		star.push_back( { 200 + r * std::cos( a ), 200 + r * std::sin( a ) } );
	}
	for( const std::vector< Vec2 >& shape : { l, star } )
	{
		std::vector< int > t = Triangulate( shape );
		CHECK( t.size() == ( shape.size() - 2 ) * 3 );
		double sum = 0;
		for( size_t k = 0; k + 2 < t.size(); k += 3 )
			sum += area( { shape[ t[ k ] ], shape[ t[ k + 1 ] ], shape[ t[ k + 2 ] ] } );
		CHECK( std::fabs( sum - area( shape ) ) < area( shape ) * 1e-4 );
	}
}

// Canvases: pictures of their own that slices take from, saved with the setup.
static void TestCanvases()
{
	AdvancedOutputSetup setup = AdvancedOutputSetup::MakeDefault( 1920, 1080 );
	Screen second;
	second.name   = "LED";
	second.width  = 3840;
	second.height = 1080;
	second.slices.push_back( AdvancedOutputSetup::MakeSlice( "Whole", 1920, 1080, 3840, 1080 ) );
	Slice part = AdvancedOutputSetup::MakeSlice( "Part", 1920, 1080, 3840, 1080 );
	part.input = { { 0, 0 }, { 960, 0 }, { 960, 540 }, { 0, 540 } };
	second.slices.push_back( part );
	setup.screens.push_back( second );
	setup.EnsureIds();

	// The LED screen's own picture: its size and name; its slices that took from the composition take from it,
	// keeping their share of it.
	std::string id = setup.MakeScreenPicture( 1, 1920, 1080 );
	const Canvas* led = setup.FindCanvas( id );
	CHECK( led && led->name == "LED" && led->width == 3840 && led->height == 1080 && led->screen == setup.screens[ 1 ].id );
	CHECK( setup.MakeScreenPicture( 1, 1920, 1080 ) == id && setup.canvases.size() == 1 ); // made once
	CHECK( setup.ScreenPictureOf( 1 ) == id && setup.ScreenPictureOf( 0 ).empty() && setup.ScreenOfPicture( id ) == 1 );
	CHECK( setup.screens[ 1 ].slices[ 0 ].source == id && Near( setup.screens[ 1 ].slices[ 0 ].input[ 2 ], { 3840, 1080 } ) );
	CHECK( setup.screens[ 1 ].slices[ 1 ].source == id && Near( setup.screens[ 1 ].slices[ 1 ].input[ 2 ], { 1920, 540 } ) );
	CHECK( setup.screens[ 0 ].slices[ 0 ].source.empty() ); // other screens untouched
	setup.SetSliceSource( setup.screens[ 1 ].slices[ 1 ], "", 1920, 1080 );
	CHECK( Near( setup.screens[ 1 ].slices[ 1 ].input[ 2 ], { 960, 540 } ) );
	int w = 0, h = 0;
	setup.SourceSize( id, 1920, 1080, w, h );
	CHECK( w == 3840 && h == 1080 );
	setup.SourceSize( "", 1920, 1080, w, h );
	CHECK( w == 1920 && h == 1080 );

	// Moving a slice to another picture keeps its share of it.
	Slice& p = setup.screens[ 1 ].slices[ 1 ];
	setup.SetSliceSource( p, id, 1920, 1080 );
	CHECK( p.source == id && Near( p.input[ 2 ], { 1920, 540 } ) );
	setup.SetSliceSource( p, "nope", 1920, 1080 );
	CHECK( p.source.empty() ); // not a canvas: the composition

	// Saved and read back; a slice naming a canvas that is not there takes from the composition.
	AdvancedOutputSetup back;
	std::string error;
	CHECK( LoadAdvancedOutput( SaveAdvancedOutput( setup ), back, error ) );
	CHECK( back.canvases.size() == 1 && back.canvases[ 0 ].id == id && back.canvases[ 0 ].width == 3840 && back.canvases[ 0 ].name == "LED" );
	CHECK( back.canvases[ 0 ].screen == setup.screens[ 1 ].id && back.screens[ 1 ].id == setup.screens[ 1 ].id );
	CHECK( back.screens[ 1 ].slices[ 0 ].source == id );
	back.canvases.clear();
	AdvancedOutputSetup dangling;
	CHECK( LoadAdvancedOutput( SaveAdvancedOutput( back ), dangling, error ) && dangling.screens[ 1 ].slices[ 0 ].source.empty() );

	// Removing a canvas sends its slices back to the composition, scaled to it.
	setup.RemoveCanvas( id, 1920, 1080 );
	CHECK( setup.canvases.empty() && setup.screens[ 1 ].slices[ 0 ].source.empty() && Near( setup.screens[ 1 ].slices[ 0 ].input[ 2 ], { 1920, 1080 } ) );
	Canvas a = setup.MakeCanvas( "A", 10, 99999 ), b = setup.MakeCanvas( "B", 640, 480 );
	CHECK( a.width == 16 && a.height == 16384 && !a.id.empty() && a.id != b.id );
}

// Screens' pictures follow their screens; the Grid's and the timeline's setups are saved together, and an older
// single setup becomes both (the Grid's without canvases, the timeline's with its canvases given to screens).
static void TestScreenPictures()
{
	AdvancedOutputSetup setup = AdvancedOutputSetup::MakeDefault( 1920, 1080 );
	Screen side;
	side.name   = "Side";
	side.width  = 1280;
	side.height = 720;
	side.slices.push_back( AdvancedOutputSetup::MakeSlice( "Whole", 1920, 1080, 1280, 720 ) );
	setup.screens.push_back( side );
	setup.EnsureIds();
	CHECK( !setup.screens[ 0 ].id.empty() && setup.screens[ 0 ].id != setup.screens[ 1 ].id );
	std::string id = setup.MakeScreenPicture( 1, 1920, 1080 );
	CHECK( !setup.SyncScreenPictures( 1920, 1080 ) ); // nothing to do

	// The screen grows: so does its picture, and the slices keep their share; renamed: so is the picture.
	setup.screens[ 1 ].width = 2560;
	setup.screens[ 1 ].name  = "Left";
	CHECK( setup.SyncScreenPictures( 1920, 1080 ) );
	CHECK( setup.FindCanvas( id )->width == 2560 && setup.FindCanvas( id )->name == "Left" );
	CHECK( Near( setup.screens[ 1 ].slices[ 0 ].input[ 2 ], { 2560, 720 } ) );

	// A free canvas (an older setup) goes to the first screen taking from it; one whose screen is gone goes.
	AdvancedOutputSetup old = AdvancedOutputSetup::MakeDefault( 1920, 1080 );
	Canvas wall = old.MakeCanvas( "Wall", 3840, 1080 );
	old.canvases.push_back( wall );
	old.screens[ 0 ].slices[ 0 ].source = wall.id;
	std::string single = SaveAdvancedOutput( old );
	AdvancedOutputSetup grid, timeline;
	std::string error;
	CHECK( LoadOutputSetups( single, grid, timeline, 1920, 1080, error ) );
	CHECK( grid.canvases.empty() && grid.screens[ 0 ].slices[ 0 ].source.empty() );
	CHECK( timeline.canvases.size() == 1 && timeline.canvases[ 0 ].screen == timeline.screens[ 0 ].id && timeline.ScreenOfPicture( wall.id ) == 0 );
	CHECK( grid.screens[ 0 ].id == timeline.screens[ 0 ].id );

	// Both saved together and read back as they were.
	timeline.screens[ 0 ].name = "Timeline screen";
	timeline.SyncScreenPictures( 1920, 1080 ); // its picture is named after it
	AdvancedOutputSetup grid2, timeline2;
	CHECK( LoadOutputSetups( SaveOutputSetups( grid, timeline ), grid2, timeline2, 1920, 1080, error ) );
	CHECK( grid2.canvases.empty() && timeline2.screens[ 0 ].name == "Timeline screen" && grid2.screens[ 0 ].name != "Timeline screen" );
	CHECK( SaveOutputSetups( grid2, timeline2 ) == SaveOutputSetups( grid, timeline ) );
	CHECK( !LoadOutputSetups( "{}", grid2, timeline2, 1920, 1080, error ) );

	// The screen deleted: its picture goes, and the slices taking from it on other screens take from the composition,
	// keeping their share (the slice took the left half of the 3840 wide wall, which became the screen's 1920).
	Screen copy = timeline.screens[ 0 ];
	copy.id.clear();
	timeline.screens.push_back( copy );
	timeline.screens.erase( timeline.screens.begin() );
	CHECK( timeline.SyncScreenPictures( 1920, 1080 ) );
	CHECK( timeline.canvases.empty() && timeline.screens[ 0 ].slices[ 0 ].source.empty() && Near( timeline.screens[ 0 ].slices[ 0 ].input[ 2 ], { 960, 1080 } ) );
}

int main()
{
	TestCanvases();
	TestScreenPictures();
	TestPolygonShapes();
	TestPerspective();
	TestDisplayIds();
	TestChaserStage();
	TestGrids();
	TestContour();
	TestMasks();
	TestPolygons();
	TestSoftEdge();
	TestSaveLoad();
	TestResolumeImport();
	TestResolumeWarpImport();
	if( failures )
	{
		std::fprintf( stderr, "%d check(s) failed\n", failures );
		return 1;
	}
	std::printf( "output tests passed\n" );
	return 0;
}
