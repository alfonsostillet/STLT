// Tests for the advanced output without a GPU: warps, polygons, soft edges, saving and Resolume import.
#include <cmath>
#include <cstdio>
#include <string>
#include "output/AdvancedOutput.h"

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

int main()
{
	TestPerspective();
	TestGrids();
	TestPolygons();
	TestSoftEdge();
	TestSaveLoad();
	TestResolumeImport();
	if( failures )
	{
		std::fprintf( stderr, "%d check(s) failed\n", failures );
		return 1;
	}
	std::printf( "output tests passed\n" );
	return 0;
}
