#include "output/Stage.h"
#include "output/AdvancedOutput.h"
#include <algorithm>
#include <cmath>
#include <json.hpp>

namespace stltvj
{
namespace
{
constexpr float kDegrees = 3.14159265f / 180.0f;

Vec3 Add( Vec3 a, Vec3 b ) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
Vec3 Sub( Vec3 a, Vec3 b ) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
Vec3 Mul( Vec3 a, float s ) { return { a.x * s, a.y * s, a.z * s }; }
float Dot( Vec3 a, Vec3 b ) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Cross( Vec3 a, Vec3 b ) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
float Length( Vec3 a ) { return std::sqrt( Dot( a, a ) ); }
Vec3 Normalise( Vec3 a )
{
	float l = Length( a );
	return l > 1e-9f ? Mul( a, 1 / l ) : Vec3{ 0, 0, 0 };
}

// Rotation by yaw (around y), pitch (around x), roll (around z), applied roll first, then pitch, then yaw.
Vec3 Rotate( Vec3 v, Vec3 degrees )
{
	float cy = std::cos( degrees.x * kDegrees ), sy = std::sin( degrees.x * kDegrees );
	float cp = std::cos( degrees.y * kDegrees ), sp = std::sin( degrees.y * kDegrees );
	float cr = std::cos( degrees.z * kDegrees ), sr = std::sin( degrees.z * kDegrees );
	Vec3 r = { v.x * cr - v.y * sr, v.x * sr + v.y * cr, v.z };     // roll
	Vec3 p = { r.x, r.y * cp - r.z * sp, r.y * sp + r.z * cp };      // pitch: -90 lays a plane flat, facing up
	return { p.x * cy + p.z * sy, p.y, -p.x * sy + p.z * cy };       // yaw
}

StageFace MakeFace( int surface, int face, Vec3 centre, Vec3 ex, Vec3 ey, float width, float height )
{
	StageFace f;
	f.surface = surface;
	f.face    = face;
	f.ex      = ex;
	f.ey      = ey;
	f.width   = width;
	f.height  = height;
	f.origin  = Sub( Sub( centre, Mul( ex, width / 2 ) ), Mul( ey, height / 2 ) );
	return f;
}

// Sutherland-Hodgman: the part of a convex polygon on the side of a plane (through `point`, `normal`) the normal
// points to.
std::vector< Vec3 > ClipByPlane( const std::vector< Vec3 >& polygon, Vec3 point, Vec3 normal )
{
	std::vector< Vec3 > out;
	size_t n = polygon.size();
	for( size_t i = 0; i < n; ++i )
	{
		Vec3 a = polygon[ i ], b = polygon[ ( i + 1 ) % n ];
		float da = Dot( Sub( a, point ), normal ), db = Dot( Sub( b, point ), normal );
		if( da >= 0 )
			out.push_back( a );
		if( ( da >= 0 ) != ( db >= 0 ) )
		{
			float t = da / ( da - db );
			out.push_back( Add( a, Mul( Sub( b, a ), t ) ) );
		}
	}
	return out;
}

// The polygon in the face's own metres (x along ex, y along ey).
std::vector< Vec2 > OnFace( const StageFace& face, const std::vector< Vec3 >& polygon )
{
	std::vector< Vec2 > out;
	for( Vec3 p : polygon )
	{
		Vec3 d = Sub( p, face.origin );
		out.push_back( { Dot( d, face.ex ), Dot( d, face.ey ) } );
	}
	return out;
}

float Area2( const std::vector< Vec2 >& polygon )
{
	float a = 0;
	for( size_t i = 0, n = polygon.size(); i < n; ++i )
	{
		const Vec2 &p = polygon[ i ], &q = polygon[ ( i + 1 ) % n ];
		a += p.x * q.y - q.x * p.y;
	}
	return std::abs( a ) / 2;
}

bool InsideConvex( const std::vector< Vec2 >& polygon, Vec2 p )
{
	if( polygon.size() < 3 )
		return false;
	int sign = 0;
	for( size_t i = 0, n = polygon.size(); i < n; ++i )
	{
		const Vec2 &a = polygon[ i ], &b = polygon[ ( i + 1 ) % n ];
		float c = ( b.x - a.x ) * ( p.y - a.y ) - ( b.y - a.y ) * ( p.x - a.x );
		if( std::abs( c ) < 1e-9f )
			continue;
		int s = c > 0 ? 1 : -1;
		if( sign == 0 )
			sign = s;
		else if( s != sign )
			return false;
	}
	return true;
}
}

Vec3 StageFace::Normal() const
{
	return Normalise( Cross( ex, ey ) );
}

Vec3 StageFace::Corner( int i ) const
{
	switch( i & 3 )
	{
	case 1: return Add( origin, Mul( ex, width ) );
	case 2: return Add( Add( origin, Mul( ex, width ) ), Mul( ey, height ) );
	case 3: return Add( origin, Mul( ey, height ) );
	default: return origin;
	}
}

Stage Stage::MakeDefault()
{
	Stage stage;
	StageSurface facade;
	facade.name     = "Façade";
	facade.position = { 0, 6, 0 };
	facade.size     = { 20, 12, 0 };
	stage.surfaces.push_back( facade );
	StageSurface ground;
	ground.name     = "Ground";
	ground.position = { 0, 0, 15 };
	ground.size     = { 40, 30, 0 };
	ground.rotation = { 0, -90, 0 }; // facing up
	stage.surfaces.push_back( ground );
	return stage;
}

std::vector< StageFace > Faces( const Stage& stage )
{
	std::vector< StageFace > faces;
	for( int s = 0; s < (int)stage.surfaces.size(); ++s )
	{
		const StageSurface& surface = stage.surfaces[ s ];
		Vec3 ex = Rotate( { 1, 0, 0 }, surface.rotation ), ey = Rotate( { 0, 1, 0 }, surface.rotation ), ez = Rotate( { 0, 0, 1 }, surface.rotation );
		float w = std::max( 0.0f, surface.size.x ), h = std::max( 0.0f, surface.size.y ), d = std::max( 0.0f, surface.size.z );
		Vec3 c = surface.position;
		if( surface.kind == StageSurface::Kind::Plane )
		{
			faces.push_back( MakeFace( s, 0, c, ex, ey, w, h ) );
			continue;
		}
		// A box: each face seen from outside, ex to the right and ey up (top and bottom: ey away from the front).
		faces.push_back( MakeFace( s, 0, Add( c, Mul( ez, d / 2 ) ), ex, ey, w, h ) );                     // front
		faces.push_back( MakeFace( s, 1, Sub( c, Mul( ez, d / 2 ) ), Mul( ex, -1 ), ey, w, h ) );          // back
		faces.push_back( MakeFace( s, 2, Sub( c, Mul( ex, w / 2 ) ), ez, ey, d, h ) );                     // left
		faces.push_back( MakeFace( s, 3, Add( c, Mul( ex, w / 2 ) ), Mul( ez, -1 ), ey, d, h ) );          // right
		faces.push_back( MakeFace( s, 4, Add( c, Mul( ey, h / 2 ) ), ex, Mul( ez, -1 ), w, d ) );          // top
		faces.push_back( MakeFace( s, 5, Sub( c, Mul( ey, h / 2 ) ), ex, ez, w, d ) );                     // bottom
	}
	return faces;
}

ProjectorFrame FrameOf( const StageProjector& projector )
{
	ProjectorFrame f;
	f.position = projector.position;
	float pan = projector.pan * kDegrees, tilt = projector.tilt * kDegrees, roll = projector.roll * kDegrees;
	f.forward = Normalise( { std::sin( pan ) * std::cos( tilt ), std::sin( tilt ), -std::cos( pan ) * std::cos( tilt ) } );
	Vec3 right = Normalise( Cross( f.forward, { 0, 1, 0 } ) );
	if( Length( right ) < 0.5f ) // straight up or down
		right = { std::cos( pan ), 0, std::sin( pan ) };
	Vec3 up = Cross( right, f.forward );
	f.right = Add( Mul( right, std::cos( roll ) ), Mul( up, std::sin( roll ) ) );
	f.up    = Add( Mul( up, std::cos( roll ) ), Mul( right, -std::sin( roll ) ) );
	float ratio  = std::max( 0.05f, projector.throwRatio );
	f.halfWidth  = 0.5f / ratio;
	f.halfHeight = f.halfWidth * (float)std::max( 1, projector.height ) / (float)std::max( 1, projector.width );
	f.centreX    = projector.shiftX * 2 * f.halfWidth;
	f.centreY    = projector.shiftY * 2 * f.halfHeight;
	return f;
}

Vec3 RayThrough( const ProjectorFrame& f, float u, float v )
{
	return Add( Add( f.forward, Mul( f.right, f.centreX + u * f.halfWidth ) ), Mul( f.up, f.centreY + v * f.halfHeight ) );
}

bool ProjectToPicture( const StageProjector& projector, Vec3 point, Vec2& pixel )
{
	ProjectorFrame f = FrameOf( projector );
	Vec3 d  = Sub( point, f.position );
	float z = Dot( d, f.forward );
	if( z <= 1e-4f )
		return false;
	float u = ( Dot( d, f.right ) / z - f.centreX ) / f.halfWidth, v = ( Dot( d, f.up ) / z - f.centreY ) / f.halfHeight;
	pixel.x = ( u + 1 ) / 2 * projector.width;
	pixel.y = ( 1 - v ) / 2 * projector.height;
	return true;
}

float PolygonArea( const std::vector< Vec3 >& polygon )
{
	Vec3 sum;
	for( size_t i = 0, n = polygon.size(); i < n; ++i )
		sum = Add( sum, Cross( polygon[ i ], polygon[ ( i + 1 ) % n ] ) );
	return Length( sum ) / 2;
}

std::vector< Lighting > LightingOf( const std::vector< StageFace >& faces, const StageProjector& projector )
{
	std::vector< Lighting > lit;
	ProjectorFrame f = FrameOf( projector );
	// The four sides of the beam, as planes through the lens facing inwards, and a near plane.
	Vec3 corner[ 4 ] = { RayThrough( f, -1, -1 ), RayThrough( f, 1, -1 ), RayThrough( f, 1, 1 ), RayThrough( f, -1, 1 ) };
	Vec3 sides[ 4 ];
	for( int i = 0; i < 4; ++i )
		sides[ i ] = Normalise( Cross( corner[ ( i + 1 ) % 4 ], corner[ i ] ) );
	// Inward: the beam's middle on the positive side.
	Vec3 middle = RayThrough( f, 0, 0 );
	for( Vec3& n : sides )
		if( Dot( n, middle ) < 0 )
			n = Mul( n, -1 );
	float pictureArea = (float)projector.width * projector.height;
	for( int i = 0; i < (int)faces.size(); ++i )
	{
		const StageFace& face = faces[ i ];
		if( face.width <= 0 || face.height <= 0 )
			continue;
		// Only a face turned towards the projector catches its light.
		if( Dot( face.Normal(), Sub( f.position, face.origin ) ) <= 0 )
			continue;
		std::vector< Vec3 > polygon = { face.Corner( 0 ), face.Corner( 1 ), face.Corner( 2 ), face.Corner( 3 ) };
		polygon = ClipByPlane( polygon, Add( f.position, Mul( f.forward, 0.05f ) ), f.forward );
		for( int s = 0; s < 4 && polygon.size() >= 3; ++s )
			polygon = ClipByPlane( polygon, f.position, sides[ s ] );
		if( polygon.size() < 3 )
			continue;
		Lighting l;
		l.face    = i;
		l.polygon = polygon;
		l.area    = PolygonArea( polygon );
		if( l.area < 1e-4f )
			continue;
		// How much of the picture falls there: the lit part seen from the lens, in its pixels.
		std::vector< Vec2 > pixels;
		for( Vec3 p : polygon )
		{
			Vec2 px;
			if( ProjectToPicture( projector, p, px ) )
				pixels.push_back( px );
		}
		float pixelArea  = pixels.size() == polygon.size() ? Area2( pixels ) : 0;
		l.share          = pictureArea > 0 ? std::min( 1.0f, pixelArea / pictureArea ) : 0;
		l.pixelsPerMetre = std::sqrt( pixelArea / l.area );
		l.lux            = projector.lumens * l.share / l.area;
		lit.push_back( std::move( l ) );
	}
	std::sort( lit.begin(), lit.end(), []( const Lighting& a, const Lighting& b ) { return a.area > b.area; } );
	return lit;
}

std::vector< Vec3 > Overlap( const StageFace& face, const std::vector< Vec3 >& a, const std::vector< Vec3 >& b )
{
	if( a.size() < 3 || b.size() < 3 )
		return {};
	// Clip a by each edge of b, as planes standing on the face.
	std::vector< Vec3 > out = a;
	Vec3 normal             = face.Normal();
	// b's winding on the face decides which side of its edges is inside.
	Vec3 middle;
	for( Vec3 p : b )
		middle = Add( middle, p );
	middle = Mul( middle, 1.0f / b.size() );
	for( size_t i = 0, n = b.size(); i < n && out.size() >= 3; ++i )
	{
		Vec3 edge   = Sub( b[ ( i + 1 ) % n ], b[ i ] );
		Vec3 inward = Normalise( Cross( normal, edge ) );
		if( Dot( inward, Sub( middle, b[ i ] ) ) < 0 )
			inward = Mul( inward, -1 );
		out = ClipByPlane( out, b[ i ], inward );
	}
	return out.size() >= 3 && PolygonArea( out ) > 1e-4f ? out : std::vector< Vec3 >{};
}

void Coverage( const StageFace& face, const std::vector< std::vector< Vec3 > >& lit, float& covered, float& overlapped )
{
	covered = overlapped = 0;
	if( face.width <= 0 || face.height <= 0 )
		return;
	std::vector< std::vector< Vec2 > > flat;
	for( const std::vector< Vec3 >& polygon : lit )
		flat.push_back( OnFace( face, polygon ) );
	const int n = 48;
	int once = 0, twice = 0;
	for( int j = 0; j < n; ++j )
		for( int i = 0; i < n; ++i )
		{
			Vec2 p{ ( i + 0.5f ) / n * face.width, ( j + 0.5f ) / n * face.height };
			int count = 0;
			for( const std::vector< Vec2 >& polygon : flat )
				count += InsideConvex( polygon, p ) ? 1 : 0;
			once += count >= 1;
			twice += count >= 2;
		}
	covered    = (float)once / ( n * n );
	overlapped = (float)twice / ( n * n );
}

// ---- Saved with the setup

namespace
{
using nlohmann::json;
json Save3( Vec3 v ) { return json::array( { v.x, v.y, v.z } ); }
Vec3 Load3( const json& j, Vec3 fallback )
{
	if( !j.is_array() || j.size() != 3 || !j[ 0 ].is_number() || !j[ 1 ].is_number() || !j[ 2 ].is_number() )
		return fallback;
	Vec3 v{ j[ 0 ].get< float >(), j[ 1 ].get< float >(), j[ 2 ].get< float >() };
	return std::isfinite( v.x ) && std::isfinite( v.y ) && std::isfinite( v.z ) ? v : fallback;
}
float Finite( const json& j, const char* key, float fallback, float lo, float hi )
{
	float v = j.contains( key ) && j[ key ].is_number() ? j[ key ].get< float >() : fallback;
	return std::isfinite( v ) ? std::clamp( v, lo, hi ) : fallback;
}
}

std::string SaveStage( const Stage& stage )
{
	json root     = json::object();
	json surfaces = json::array();
	for( const StageSurface& s : stage.surfaces )
		surfaces.push_back( { { "id", s.id },
		                      { "name", s.name },
		                      { "kind", s.kind == StageSurface::Kind::Box ? "box" : "plane" },
		                      { "position", Save3( s.position ) },
		                      { "size", Save3( s.size ) },
		                      { "rotation", Save3( s.rotation ) } } );
	root[ "surfaces" ] = surfaces;
	json projectors    = json::array();
	for( const StageProjector& p : stage.projectors )
		projectors.push_back( { { "id", p.id },
		                        { "name", p.name },
		                        { "position", Save3( p.position ) },
		                        { "aim", Save3( { p.pan, p.tilt, p.roll } ) },
		                        { "resolution", { p.width, p.height } },
		                        { "throwRatio", p.throwRatio },
		                        { "throwRange", { p.throwMin, p.throwMax } },
		                        { "shift", { p.shiftX, p.shiftY } },
		                        { "lumens", p.lumens },
		                        { "screen", p.screen } } );
	root[ "projectors" ] = projectors;
	return root.dump();
}

bool LoadStage( const std::string& text, Stage& out )
{
	json root = json::parse( text, nullptr, false );
	if( root.is_discarded() || !root.is_object() )
		return false;
	Stage stage;
	if( root.contains( "surfaces" ) && root[ "surfaces" ].is_array() )
		for( const json& j : root[ "surfaces" ] )
		{
			if( !j.is_object() )
				continue;
			StageSurface s;
			s.id       = j.value( "id", "" );
			s.name     = j.value( "name", s.name );
			s.kind     = j.value( "kind", "plane" ) == "box" ? StageSurface::Kind::Box : StageSurface::Kind::Plane;
			s.position = Load3( j.value( "position", json() ), s.position );
			s.size     = Load3( j.value( "size", json() ), s.size );
			s.size     = { std::clamp( s.size.x, 0.0f, 10000.0f ), std::clamp( s.size.y, 0.0f, 10000.0f ), std::clamp( s.size.z, 0.0f, 10000.0f ) };
			s.rotation = Load3( j.value( "rotation", json() ), s.rotation );
			stage.surfaces.push_back( s );
		}
	if( root.contains( "projectors" ) && root[ "projectors" ].is_array() )
		for( const json& j : root[ "projectors" ] )
		{
			if( !j.is_object() )
				continue;
			StageProjector p;
			p.id       = j.value( "id", "" );
			p.name     = j.value( "name", p.name );
			p.position = Load3( j.value( "position", json() ), p.position );
			Vec3 aim   = Load3( j.value( "aim", json() ), { 0, 0, 0 } );
			p.pan      = aim.x;
			p.tilt     = std::clamp( aim.y, -90.0f, 90.0f );
			p.roll     = aim.z;
			if( j.contains( "resolution" ) && j[ "resolution" ].is_array() && j[ "resolution" ].size() == 2 && j[ "resolution" ][ 0 ].is_number()
			    && j[ "resolution" ][ 1 ].is_number() )
			{
				p.width  = std::clamp( j[ "resolution" ][ 0 ].get< int >(), 16, 16384 );
				p.height = std::clamp( j[ "resolution" ][ 1 ].get< int >(), 16, 16384 );
			}
			p.throwRatio = Finite( j, "throwRatio", p.throwRatio, 0.05f, 20.0f );
			if( j.contains( "throwRange" ) && j[ "throwRange" ].is_array() && j[ "throwRange" ].size() == 2 && j[ "throwRange" ][ 0 ].is_number()
			    && j[ "throwRange" ][ 1 ].is_number() )
			{
				p.throwMin = std::clamp( j[ "throwRange" ][ 0 ].get< float >(), 0.05f, 20.0f );
				p.throwMax = std::clamp( j[ "throwRange" ][ 1 ].get< float >(), p.throwMin, 20.0f );
			}
			if( j.contains( "shift" ) && j[ "shift" ].is_array() && j[ "shift" ].size() == 2 && j[ "shift" ][ 0 ].is_number() && j[ "shift" ][ 1 ].is_number() )
			{
				p.shiftX = std::clamp( j[ "shift" ][ 0 ].get< float >(), -2.0f, 2.0f );
				p.shiftY = std::clamp( j[ "shift" ][ 1 ].get< float >(), -2.0f, 2.0f );
			}
			p.lumens = Finite( j, "lumens", p.lumens, 0.0f, 1e6f );
			p.screen = j.value( "screen", "" );
			stage.projectors.push_back( p );
		}
	out = std::move( stage );
	return true;
}
}
