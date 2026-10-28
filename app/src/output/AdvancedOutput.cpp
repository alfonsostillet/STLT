#include "AdvancedOutput.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <json.hpp>

namespace stltvj
{
namespace
{
Vec2 Lerp( Vec2 a, Vec2 b, float t )
{
	return { a.x + ( b.x - a.x ) * t, a.y + ( b.y - a.y ) * t };
}

// Bilinear inside a quad given as top-left, top-right, bottom-right, bottom-left.
Vec2 Bilinear( const Vec2* q, float u, float v )
{
	return Lerp( Lerp( q[ 0 ], q[ 1 ], u ), Lerp( q[ 3 ], q[ 2 ], u ), v );
}

// The projective map from the unit square onto a quad (Heckbert's square-to-quad).
Vec2 Projective( const Vec2* q, float u, float v )
{
	double x0 = q[ 0 ].x, y0 = q[ 0 ].y, x1 = q[ 1 ].x, y1 = q[ 1 ].y, x2 = q[ 2 ].x, y2 = q[ 2 ].y, x3 = q[ 3 ].x, y3 = q[ 3 ].y;
	double dx1 = x1 - x2, dx2 = x3 - x2, dx3 = x0 - x1 + x2 - x3;
	double dy1 = y1 - y2, dy2 = y3 - y2, dy3 = y0 - y1 + y2 - y3;
	double den = dx1 * dy2 - dx2 * dy1;
	double g = 0, h = 0;
	if( std::fabs( den ) > 1e-12 && ( std::fabs( dx3 ) > 1e-9 || std::fabs( dy3 ) > 1e-9 ) )
	{
		g = ( dx3 * dy2 - dx2 * dy3 ) / den;
		h = ( dx1 * dy3 - dx3 * dy1 ) / den;
	}
	double a = x1 - x0 + g * x1, b = x3 - x0 + h * x3, c = x0;
	double d = y1 - y0 + g * y1, e = y3 - y0 + h * y3, f = y0;
	double w = g * u + h * v + 1;
	if( std::fabs( w ) < 1e-9 )
		w = 1e-9;
	return { (float)( ( a * u + b * v + c ) / w ), (float)( ( d * u + e * v + f ) / w ) };
}

// Catmull-Rom through p1 and p2 with neighbours p0 and p3.
Vec2 CatmullRom( Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, float t )
{
	float t2 = t * t, t3 = t2 * t;
	auto f = [ & ]( float a, float b, float c, float d ) {
		return 0.5f * ( 2 * b + ( -a + c ) * t + ( 2 * a - 5 * b + 4 * c - d ) * t2 + ( -a + 3 * b - 3 * c + d ) * t3 );
	};
	return { f( p0.x, p1.x, p2.x, p3.x ), f( p0.y, p1.y, p2.y, p3.y ) };
}

// Grid point with the grid continued linearly past its edges (keeps the curve straight at the border).
Vec2 GridAt( const Slice& s, int column, int row )
{
	int c = std::clamp( column, 0, s.gridColumns - 1 ), r = std::clamp( row, 0, s.gridRows - 1 );
	Vec2 p = s.grid[ r * s.gridColumns + c ];
	if( column != c && s.gridColumns > 1 )
	{
		int inner = column < 0 ? 1 : s.gridColumns - 2;
		Vec2 q    = s.grid[ r * s.gridColumns + inner ];
		p         = { 2 * p.x - q.x, 2 * p.y - q.y };
	}
	if( row != r && s.gridRows > 1 )
	{
		int inner = row < 0 ? 1 : s.gridRows - 2;
		Vec2 q    = GridAt( s, column, inner );
		Vec2 e    = GridAt( s, column, r );
		p         = { 2 * e.x - q.x, 2 * e.y - q.y };
	}
	return p;
}

Vec2 Add( Vec2 a, Vec2 b )
{
	return { a.x + b.x, a.y + b.y };
}

Vec2 Sub( Vec2 a, Vec2 b )
{
	return { a.x - b.x, a.y - b.y };
}

float Distance( Vec2 a, Vec2 b )
{
	return std::hypot( a.x - b.x, a.y - b.y );
}

// One segment of a contour, from point a to point b, at t in 0..1.
Vec2 SegmentAt( const ContourPoint& a, const ContourPoint& b, float t, PointMode mode )
{
	if( mode == PointMode::Linear )
		return Lerp( a.p, b.p, t );
	Vec2 p1 = Add( a.p, a.out ), p2 = Add( b.p, b.in );
	float s = 1 - t;
	float w0 = s * s * s, w1 = 3 * s * s * t, w2 = 3 * s * t * t, w3 = t * t * t;
	return { w0 * a.p.x + w1 * p1.x + w2 * p2.x + w3 * b.p.x, w0 * a.p.y + w1 * p1.y + w2 * p2.y + w3 * b.p.y };
}

// A side of the contour sampled densely, walked by arc length.
struct SideCurve
{
	std::vector< Vec2 > points;
	std::vector< float > lengths; // cumulative
	void Add( Vec2 p )
	{
		lengths.push_back( points.empty() ? 0 : lengths.back() + Distance( points.back(), p ) );
		points.push_back( p );
	}
	// The point a fraction f of the way along.
	Vec2 At( float f ) const
	{
		if( points.size() < 2 || lengths.back() <= 0 )
			return points.empty() ? Vec2{} : points.front();
		float target = std::clamp( f, 0.0f, 1.0f ) * lengths.back();
		size_t i     = std::upper_bound( lengths.begin(), lengths.end(), target ) - lengths.begin();
		i            = std::clamp< size_t >( i, 1, points.size() - 1 );
		float span   = lengths[ i ] - lengths[ i - 1 ];
		return Lerp( points[ i - 1 ], points[ i ], span > 0 ? ( target - lengths[ i - 1 ] ) / span : 0 );
	}
};

// The output shape of a contour slice. The four corners give a perspective (so a plain four-corner contour is
// exactly the perspective warp); how far each side bends away from its straight edge is blended across the
// inside (a Coons patch of the bends), so the picture follows the outline.
class ContourShape
{
public:
	explicit ContourShape( const Slice& s )
	{
		int n = (int)s.contour.size();
		for( int k = 0; k < 4; ++k )
			corner[ k ] = s.contour[ s.contourCorners[ k ] ].p;
		for( int side = 0; side < 4; ++side )
		{
			SideCurve curve;
			int from = s.contourCorners[ side ], to = side == 3 ? n : s.contourCorners[ side + 1 ];
			for( int i = from; i < to; ++i )
			{
				const ContourPoint& a = s.contour[ i ];
				const ContourPoint& b = s.contour[ ( i + 1 ) % n ];
				int steps             = s.pointMode == PointMode::Linear ? 1 : 24;
				for( int k = ( i == from ? 0 : 1 ); k <= steps; ++k )
					curve.Add( SegmentAt( a, b, (float)k / steps, s.pointMode ) );
			}
			raw[ side ] = curve;
		}
		// Top and right run as stored (clockwise); bottom and left are walked from the left and the top.
		sides[ 0 ] = raw[ 0 ];
		sides[ 1 ] = raw[ 1 ];
		sides[ 2 ] = Reversed( raw[ 2 ] );
		sides[ 3 ] = Reversed( raw[ 3 ] );
	}

	Vec2 Map( float u, float v ) const
	{
		Vec2 base = Projective( corner, u, v );
		Vec2 top = Bend( sides[ 0 ], corner[ 0 ], corner[ 1 ], Projective( corner, u, 0 ) );
		Vec2 bottom = Bend( sides[ 2 ], corner[ 3 ], corner[ 2 ], Projective( corner, u, 1 ) );
		Vec2 left = Bend( sides[ 3 ], corner[ 0 ], corner[ 3 ], Projective( corner, 0, v ) );
		Vec2 right = Bend( sides[ 1 ], corner[ 1 ], corner[ 2 ], Projective( corner, 1, v ) );
		return { base.x + ( 1 - v ) * top.x + v * bottom.x + ( 1 - u ) * left.x + u * right.x,
			     base.y + ( 1 - v ) * top.y + v * bottom.y + ( 1 - u ) * left.y + u * right.y };
	}

	// The outline, clockwise from the top-left corner.
	std::vector< Vec2 > Outline() const
	{
		std::vector< Vec2 > outline;
		for( const SideCurve& side : raw )
			outline.insert( outline.end(), side.points.begin(), side.points.empty() ? side.points.end() : side.points.end() - 1 );
		return outline;
	}

private:
	static SideCurve Reversed( const SideCurve& c )
	{
		SideCurve r;
		for( auto it = c.points.rbegin(); it != c.points.rend(); ++it )
			r.Add( *it );
		return r;
	}

	// How far the side is from the straight edge a-b at the point `onEdge` of that edge.
	static Vec2 Bend( const SideCurve& side, Vec2 a, Vec2 b, Vec2 onEdge )
	{
		float length = Distance( a, b );
		float f      = length > 0 ? Distance( a, onEdge ) / length : 0;
		return Sub( side.At( f ), onEdge );
	}

	Vec2 corner[ 4 ];
	SideCurve raw[ 4 ];
	SideCurve sides[ 4 ];
};

float Cross( Vec2 o, Vec2 a, Vec2 b )
{
	return ( a.x - o.x ) * ( b.y - o.y ) - ( a.y - o.y ) * ( b.x - o.x );
}

float Area( const std::vector< Vec2 >& p )
{
	float area = 0;
	for( size_t i = 0, j = p.size() - 1; i < p.size(); j = i++ )
		area += p[ j ].x * p[ i ].y - p[ i ].x * p[ j ].y;
	return area / 2;
}
}

const char* WarpModeName( WarpMode mode )
{
	static const char* names[] = { "Perspective", "Grid", "Curved Grid", "Contour" };
	return names[ std::clamp( (int)mode, 0, (int)WarpMode::Count - 1 ) ];
}

const char* PointModeName( PointMode mode )
{
	static const char* names[] = { "Linear", "Bezier" };
	return names[ std::clamp( (int)mode, 0, (int)PointMode::Count - 1 ) ];
}

const char* OutputDeviceName( OutputDevice device )
{
	static const char* names[] = { "Virtual Output", "Window", "Display" };
	return names[ std::clamp( (int)device, 0, (int)OutputDevice::Count - 1 ) ];
}

float SoftEdge::Factor( float u, float v ) const
{
	auto ramp = [ & ]( float distance, float width ) {
		if( width <= 0 )
			return 1.0f;
		float x = std::clamp( distance / width, 0.0f, 1.0f );
		float f = x < 0.5f ? luminance * std::pow( 2 * x, power ) : 1 - ( 1 - luminance ) * std::pow( 2 * ( 1 - x ), power );
		return std::pow( std::max( f, 0.0f ), 1 / std::max( gamma, 0.1f ) );
	};
	return ramp( u, left ) * ramp( 1 - u, right ) * ramp( v, top ) * ramp( 1 - v, bottom );
}

static bool ContourUsable( const Slice& s )
{
	int n = (int)s.contour.size();
	if( n < 4 || s.contourCorners[ 0 ] != 0 )
		return false;
	for( int k = 1; k < 4; ++k )
		if( s.contourCorners[ k ] <= s.contourCorners[ k - 1 ] || s.contourCorners[ k ] >= n )
			return false;
	return true;
}

Vec2 Slice::Map( float u, float v ) const
{
	if( warp == WarpMode::Contour && ContourUsable( *this ) )
		return ContourShape( *this ).Map( u, v );
	if( warp == WarpMode::Perspective || warp == WarpMode::Contour || (int)grid.size() != gridColumns * gridRows || gridColumns < 2 || gridRows < 2 )
		return Projective( corners, u, v );
	float su = std::clamp( u, 0.0f, 1.0f ) * ( gridColumns - 1 ), sv = std::clamp( v, 0.0f, 1.0f ) * ( gridRows - 1 );
	int c = std::min( (int)su, gridColumns - 2 ), r = std::min( (int)sv, gridRows - 2 );
	float tu = su - c, tv = sv - r;
	if( warp == WarpMode::Grid )
	{
		Vec2 q[ 4 ] = { GridAt( *this, c, r ), GridAt( *this, c + 1, r ), GridAt( *this, c + 1, r + 1 ), GridAt( *this, c, r + 1 ) };
		return Bilinear( q, tu, tv );
	}
	Vec2 rows[ 4 ];
	for( int k = 0; k < 4; ++k )
		rows[ k ] = CatmullRom( GridAt( *this, c - 1, r - 1 + k ), GridAt( *this, c, r - 1 + k ), GridAt( *this, c + 1, r - 1 + k ),
		                        GridAt( *this, c + 2, r - 1 + k ), tu );
	return CatmullRom( rows[ 0 ], rows[ 1 ], rows[ 2 ], rows[ 3 ], tv );
}

std::vector< Vec2 > Slice::MapGrid( int n ) const
{
	std::vector< Vec2 > positions( (size_t)( n + 1 ) * ( n + 1 ) );
	if( warp == WarpMode::Contour && ContourUsable( *this ) )
	{
		ContourShape shape( *this ); // built once for the whole lattice
		for( int r = 0; r <= n; ++r )
			for( int c = 0; c <= n; ++c )
				positions[ (size_t)r * ( n + 1 ) + c ] = shape.Map( (float)c / n, (float)r / n );
		return positions;
	}
	for( int r = 0; r <= n; ++r )
		for( int c = 0; c <= n; ++c )
			positions[ (size_t)r * ( n + 1 ) + c ] = Map( (float)c / n, (float)r / n );
	return positions;
}

Vec2 Slice::Source( float u, float v ) const
{
	if( flipHorizontal )
		u = 1 - u;
	if( flipVertical )
		v = 1 - v;
	if( input.size() < 4 )
		return {};
	return Bilinear( input.data(), u, v );
}

void Slice::ResampleGrid( int columns, int rows )
{
	columns = std::clamp( columns, 2, 32 );
	rows    = std::clamp( rows, 2, 32 );
	std::vector< Vec2 > fresh( (size_t)columns * rows );
	for( int r = 0; r < rows; ++r )
		for( int c = 0; c < columns; ++c )
			fresh[ (size_t)r * columns + c ] = Map( (float)c / ( columns - 1 ), (float)r / ( rows - 1 ) );
	gridColumns = columns;
	gridRows    = rows;
	grid        = std::move( fresh );
}

void Slice::SetWarp( WarpMode mode )
{
	if( mode == warp )
		return;
	if( mode == WarpMode::Contour )
	{
		// The outline as it is now: its corners, plus the grid's border points when coming from a grid.
		bool fromGrid = warp != WarpMode::Perspective && (int)grid.size() == gridColumns * gridRows && gridColumns >= 2 && gridRows >= 2;
		std::vector< ContourPoint > fresh;
		auto add = [ & ]( Vec2 p ) { fresh.push_back( { p, {}, {} } ); };
		int columns = fromGrid ? gridColumns : 2, rows = fromGrid ? gridRows : 2;
		for( int c = 0; c < columns - 1; ++c )
			add( Map( (float)c / ( columns - 1 ), 0 ) );
		contourCorners[ 1 ] = (int)fresh.size();
		for( int r = 0; r < rows - 1; ++r )
			add( Map( 1, (float)r / ( rows - 1 ) ) );
		contourCorners[ 2 ] = (int)fresh.size();
		for( int c = columns - 1; c > 0; --c )
			add( Map( (float)c / ( columns - 1 ), 1 ) );
		contourCorners[ 3 ] = (int)fresh.size();
		for( int r = rows - 1; r > 0; --r )
			add( Map( 0, (float)r / ( rows - 1 ) ) );
		contourCorners[ 0 ] = 0;
		contour = std::move( fresh );
		warp    = mode;
		for( int i = 0; i < (int)contour.size(); ++i )
			SmoothContourPoint( i );
		return;
	}
	if( mode == WarpMode::Perspective )
	{
		// Keep the outer corners.
		Vec2 c[ 4 ] = { Map( 0, 0 ), Map( 1, 0 ), Map( 1, 1 ), Map( 0, 1 ) };
		std::copy( c, c + 4, corners );
		warp = mode;
		return;
	}
	if( warp == WarpMode::Perspective || warp == WarpMode::Contour || (int)grid.size() != gridColumns * gridRows )
		ResampleGrid( gridColumns, gridRows );
	warp = mode;
}

bool Slice::HasContour() const
{
	return mask || ( !polygon && warp == WarpMode::Contour );
}

bool Slice::IsContourCorner( int index ) const
{
	if( mask )
		return false;
	for( int c : contourCorners )
		if( c == index )
			return true;
	return false;
}

void Slice::SmoothContourPoint( int index )
{
	int n = (int)contour.size();
	if( index < 0 || index >= n )
		return;
	ContourPoint& p = contour[ index ];
	Vec2 prev = contour[ ( index + n - 1 ) % n ].p, next = contour[ ( index + 1 ) % n ].p;
	if( IsContourCorner( index ) || n < 3 )
	{
		// Straight handles a third of the way along each side: the sides stay straight, the handles can be grabbed.
		p.in  = { ( prev.x - p.p.x ) / 3, ( prev.y - p.p.y ) / 3 };
		p.out = { ( next.x - p.p.x ) / 3, ( next.y - p.p.y ) / 3 };
		return;
	}
	// Catmull-Rom tangent through the neighbours, as Bézier handles.
	Vec2 t{ ( next.x - prev.x ) / 6, ( next.y - prev.y ) / 6 };
	p.out = t;
	p.in  = { -t.x, -t.y };
}

void Slice::ResetContour()
{
	Vec2 c[ 4 ] = { Map( 0, 0 ), Map( 1, 0 ), Map( 1, 1 ), Map( 0, 1 ) };
	contour.clear();
	for( Vec2 p : c )
		contour.push_back( { p, {}, {} } );
	for( int k = 0; k < 4; ++k )
		contourCorners[ k ] = k;
	for( int k = 0; k < 4; ++k )
		SmoothContourPoint( k );
}

int Slice::InsertContourPoint( Vec2 near, float maxDistance )
{
	int n = (int)contour.size();
	if( n < ( mask ? 3 : 4 ) )
		return -1;
	int bestSegment = -1;
	float bestT = 0, best = maxDistance;
	for( int i = 0; i < n; ++i )
		for( int k = 1; k < 64; ++k )
		{
			float t = k / 64.0f;
			float d = Distance( SegmentAt( contour[ i ], contour[ ( i + 1 ) % n ], t, pointMode ), near );
			if( d < best )
			{
				best        = d;
				bestSegment = i;
				bestT       = t;
			}
		}
	if( bestSegment < 0 )
		return -1;
	ContourPoint& a = contour[ bestSegment ];
	ContourPoint& b = contour[ ( bestSegment + 1 ) % n ];
	ContourPoint fresh;
	if( pointMode == PointMode::Bezier )
	{
		// De Casteljau split: the curve keeps its shape, the new point sits on it with matching handles.
		Vec2 p0 = a.p, p1 = Add( a.p, a.out ), p2 = Add( b.p, b.in ), p3 = b.p;
		Vec2 p01 = Lerp( p0, p1, bestT ), p12 = Lerp( p1, p2, bestT ), p23 = Lerp( p2, p3, bestT );
		Vec2 p012 = Lerp( p01, p12, bestT ), p123 = Lerp( p12, p23, bestT ), m = Lerp( p012, p123, bestT );
		a.out     = Sub( p01, p0 );
		b.in      = Sub( p23, p3 );
		fresh     = { m, Sub( p012, m ), Sub( p123, m ) };
	}
	else
	{
		Vec2 m = Lerp( a.p, b.p, bestT );
		fresh  = { m, { ( a.p.x - m.x ) / 3, ( a.p.y - m.y ) / 3 }, { ( b.p.x - m.x ) / 3, ( b.p.y - m.y ) / 3 } };
	}
	int at = bestSegment + 1;
	contour.insert( contour.begin() + at, fresh );
	for( int k = 1; k < 4; ++k )
		if( contourCorners[ k ] >= at )
			contourCorners[ k ]++;
	return at;
}

bool Slice::RemoveContourPoint( int index )
{
	if( index < 0 || index >= (int)contour.size() || IsContourCorner( index ) || ( mask && contour.size() <= 3 ) )
		return false;
	contour.erase( contour.begin() + index );
	for( int k = 1; k < 4; ++k )
		if( contourCorners[ k ] > index )
			contourCorners[ k ]--;
	return true;
}

int Slice::InsertPolygonPoint( bool onOutput, Vec2 near, float maxDistance )
{
	std::vector< Vec2 >& edit  = onOutput ? output : input;
	std::vector< Vec2 >& other = onOutput ? input : output;
	int n                      = (int)edit.size();
	if( n < 3 || (int)other.size() != n )
		return -1;
	int bestEdge = -1;
	float bestT = 0, best = maxDistance;
	for( int i = 0; i < n; ++i )
	{
		Vec2 a = edit[ i ], b = edit[ ( i + 1 ) % n ];
		Vec2 ab = Sub( b, a );
		float len2 = ab.x * ab.x + ab.y * ab.y;
		float t    = len2 > 0 ? std::clamp( ( ( near.x - a.x ) * ab.x + ( near.y - a.y ) * ab.y ) / len2, 0.0f, 1.0f ) : 0;
		float d    = Distance( Lerp( a, b, t ), near );
		if( d < best && t > 0.01f && t < 0.99f )
		{
			best     = d;
			bestEdge = i;
			bestT    = t;
		}
	}
	if( bestEdge < 0 )
		return -1;
	int at = bestEdge + 1;
	edit.insert( edit.begin() + at, Lerp( edit[ bestEdge ], edit[ ( bestEdge + 1 ) % n ], bestT ) );
	other.insert( other.begin() + at, Lerp( other[ bestEdge ], other[ ( bestEdge + 1 ) % n ], bestT ) );
	return at;
}

bool Slice::RemovePolygonPoint( int index )
{
	if( !polygon || input.size() <= 3 || index < 0 || index >= (int)input.size() || input.size() != output.size() )
		return false;
	input.erase( input.begin() + index );
	output.erase( output.begin() + index );
	return true;
}

void Slice::MatchOutputToInput( float compWidth, float compHeight, float screenWidth, float screenHeight )
{
	float sx = screenWidth / std::max( compWidth, 1.0f ), sy = screenHeight / std::max( compHeight, 1.0f );
	if( mask )
		return;
	if( polygon )
	{
		output.resize( input.size() );
		for( size_t i = 0; i < input.size(); ++i )
			output[ i ] = { input[ i ].x * sx, input[ i ].y * sy };
		return;
	}
	bool contourMode = warp == WarpMode::Contour;
	for( int i = 0; i < 4 && i < (int)input.size(); ++i )
		corners[ i ] = { input[ i ].x * sx, input[ i ].y * sy };
	warp = WarpMode::Perspective;
	grid.clear();
	if( contourMode )
	{
		ResetContour();
		warp = WarpMode::Contour;
	}
}

void Slice::FitOutput( float w, float h )
{
	if( mask )
	{
		// Scale the shape's bounds (and its handles) onto the screen.
		std::vector< Vec2 > outline = OutputOutline();
		float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
		for( Vec2 p : outline )
		{
			minX = std::min( minX, p.x );
			minY = std::min( minY, p.y );
			maxX = std::max( maxX, p.x );
			maxY = std::max( maxY, p.y );
		}
		float fx = w / std::max( maxX - minX, 1.0f ), fy = h / std::max( maxY - minY, 1.0f );
		for( ContourPoint& c : contour )
			c = { { ( c.p.x - minX ) * fx, ( c.p.y - minY ) * fy }, { c.in.x * fx, c.in.y * fy }, { c.out.x * fx, c.out.y * fy } };
		return;
	}
	if( polygon )
	{
		// Scale the polygon's bounds onto the screen.
		float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
		for( Vec2 p : output )
		{
			minX = std::min( minX, p.x );
			minY = std::min( minY, p.y );
			maxX = std::max( maxX, p.x );
			maxY = std::max( maxY, p.y );
		}
		float bw = std::max( maxX - minX, 1.0f ), bh = std::max( maxY - minY, 1.0f );
		for( Vec2& p : output )
			p = { ( p.x - minX ) / bw * w, ( p.y - minY ) / bh * h };
		return;
	}
	bool contourMode = warp == WarpMode::Contour;
	corners[ 0 ]     = { 0, 0 };
	corners[ 1 ]     = { w, 0 };
	corners[ 2 ]     = { w, h };
	corners[ 3 ]     = { 0, h };
	warp             = WarpMode::Perspective;
	grid.clear();
	if( contourMode )
	{
		ResetContour();
		warp = WarpMode::Contour;
	}
}

std::vector< Vec2 > Slice::OutputOutline( int steps ) const
{
	if( polygon )
		return output;
	if( mask )
	{
		std::vector< Vec2 > outline;
		int n        = (int)contour.size();
		int segments = pointMode == PointMode::Linear ? 1 : std::max( steps, 1 );
		for( int i = 0; i < n; ++i )
			for( int k = 0; k < segments; ++k )
				outline.push_back( SegmentAt( contour[ i ], contour[ ( i + 1 ) % n ], (float)k / segments, pointMode ) );
		return outline;
	}
	if( warp == WarpMode::Contour && ContourUsable( *this ) )
		return ContourShape( *this ).Outline();
	std::vector< Vec2 > outline;
	if( warp == WarpMode::Perspective || warp == WarpMode::Contour )
		steps = 1;
	for( int i = 0; i < steps; ++i )
		outline.push_back( Map( (float)i / steps, 0 ) );
	for( int i = 0; i < steps; ++i )
		outline.push_back( Map( 1, (float)i / steps ) );
	for( int i = 0; i < steps; ++i )
		outline.push_back( Map( 1 - (float)i / steps, 1 ) );
	for( int i = 0; i < steps; ++i )
		outline.push_back( Map( 0, 1 - (float)i / steps ) );
	return outline;
}

std::vector< Vec2 > Slice::InputOutline() const
{
	return mask ? std::vector< Vec2 >() : input;
}

Slice AdvancedOutputSetup::MakeSlice( const std::string& name, float cw, float ch, float sw, float sh )
{
	Slice s;
	s.name  = name;
	s.input = { { 0, 0 }, { cw, 0 }, { cw, ch }, { 0, ch } };
	s.FitOutput( sw, sh );
	return s;
}

Slice AdvancedOutputSetup::MakePolygon( const std::string& name, float cw, float ch, float sw, float sh )
{
	Slice s;
	s.name    = name;
	s.polygon = true;
	s.input   = { { cw * 0.5f, ch * 0.2f }, { cw * 0.8f, ch * 0.8f }, { cw * 0.2f, ch * 0.8f } };
	s.MatchOutputToInput( cw, ch, sw, sh );
	return s;
}

Slice AdvancedOutputSetup::MakeMask( const std::string& name, float sw, float sh, bool ellipse )
{
	Slice s;
	s.name = name;
	s.mask = true;
	float cx = sw / 2, cy = sh / 2, rx = sw / 4, ry = sh / 4;
	if( ellipse )
	{
		// Four points with handles of 0.5523 times the radius: a circle to within 0.03 %.
		const float k = 0.5523f;
		s.contour     = { { { cx, cy - ry }, { -rx * k, 0 }, { rx * k, 0 } },
                      { { cx + rx, cy }, { 0, -ry * k }, { 0, ry * k } },
                      { { cx, cy + ry }, { rx * k, 0 }, { -rx * k, 0 } },
                      { { cx - rx, cy }, { 0, ry * k }, { 0, -ry * k } } };
	}
	else
	{
		// Straight handles a third of the way along each side, ready to be pulled.
		s.contour = { { { cx - rx, cy - ry }, {}, {} }, { { cx + rx, cy - ry }, {}, {} }, { { cx + rx, cy + ry }, {}, {} }, { { cx - rx, cy + ry }, {}, {} } };
		for( int i = 0; i < 4; ++i )
		{
			ContourPoint& c = s.contour[ i ];
			Vec2 prev = s.contour[ ( i + 3 ) % 4 ].p, next = s.contour[ ( i + 1 ) % 4 ].p;
			c.in      = { ( prev.x - c.p.x ) / 3, ( prev.y - c.p.y ) / 3 };
			c.out     = { ( next.x - c.p.x ) / 3, ( next.y - c.p.y ) / 3 };
		}
	}
	return s;
}

AdvancedOutputSetup AdvancedOutputSetup::MakeDefault( int compWidth, int compHeight )
{
	AdvancedOutputSetup setup;
	Screen screen;
	screen.name   = "Screen 1";
	screen.width  = compWidth;
	screen.height = compHeight;
	screen.slices.push_back( MakeSlice( "Slice 1", (float)compWidth, (float)compHeight, (float)compWidth, (float)compHeight ) );
	setup.screens.push_back( screen );
	return setup;
}

// Ear clipping: fine for the few dozen points a mapping polygon has.
std::vector< int > Triangulate( const std::vector< Vec2 >& points )
{
	std::vector< int > triangles;
	int n = (int)points.size();
	if( n < 3 )
		return triangles;
	std::vector< int > index( n );
	for( int i = 0; i < n; ++i )
		index[ i ] = i;
	bool ccw = Area( points ) > 0;
	int guard = 0;
	while( index.size() > 3 && guard++ < 10000 )
	{
		bool clipped = false;
		int m        = (int)index.size();
		for( int i = 0; i < m; ++i )
		{
			int a = index[ ( i + m - 1 ) % m ], b = index[ i ], c = index[ ( i + 1 ) % m ];
			float turn = Cross( points[ a ], points[ b ], points[ c ] );
			if( ccw ? turn <= 0 : turn >= 0 )
				continue; // reflex corner
			bool inside = false;
			for( int k : index )
			{
				if( k == a || k == b || k == c )
					continue;
				float d1 = Cross( points[ a ], points[ b ], points[ k ] ), d2 = Cross( points[ b ], points[ c ], points[ k ] ),
				      d3 = Cross( points[ c ], points[ a ], points[ k ] );
				bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
				if( !( neg && pos ) )
				{
					inside = true;
					break;
				}
			}
			if( inside )
				continue;
			triangles.insert( triangles.end(), { a, b, c } );
			index.erase( index.begin() + i );
			clipped = true;
			break;
		}
		if( !clipped )
			break; // self-intersecting: give up on the rest
	}
	if( index.size() == 3 )
		triangles.insert( triangles.end(), { index[ 0 ], index[ 1 ], index[ 2 ] } );
	return triangles;
}

bool PointInPolygon( const std::vector< Vec2 >& polygon, Vec2 p )
{
	bool inside = false;
	for( size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++ )
	{
		const Vec2 &a = polygon[ i ], &b = polygon[ j ];
		if( ( ( a.y > p.y ) != ( b.y > p.y ) ) && p.x < ( b.x - a.x ) * ( p.y - a.y ) / ( b.y - a.y ) + a.x )
			inside = !inside;
	}
	return inside;
}

// ---- JSON

namespace
{
using nlohmann::json;

json Points( const std::vector< Vec2 >& points )
{
	json list = json::array();
	for( Vec2 p : points )
		list.push_back( { p.x, p.y } );
	return list;
}

std::vector< Vec2 > ReadPoints( const json& list )
{
	std::vector< Vec2 > points;
	if( !list.is_array() )
		return points;
	for( const json& p : list )
		if( p.is_array() && p.size() == 2 && p[ 0 ].is_number() && p[ 1 ].is_number() )
			points.push_back( { p[ 0 ].get< float >(), p[ 1 ].get< float >() } );
	return points;
}

template< typename Enum >
Enum ByName( const json& value, const char* ( *name )( Enum ), Enum fallback )
{
	if( !value.is_string() )
		return fallback;
	for( int i = 0; i < (int)Enum::Count; ++i )
		if( value.get< std::string >() == name( (Enum)i ) )
			return (Enum)i;
	return fallback;
}
}

std::string SaveAdvancedOutput( const AdvancedOutputSetup& setup )
{
	json root;
	root[ "format" ]  = "stlt-vj-advanced-output";
	root[ "version" ] = 1;
	json screens      = json::array();
	for( const Screen& s : setup.screens )
	{
		json j;
		j[ "name" ]        = s.name;
		j[ "enabled" ]     = s.enabled;
		j[ "device" ]      = OutputDeviceName( s.device );
		j[ "display" ]     = s.display;
		j[ "size" ]        = { s.width, s.height };
		j[ "opacity" ]     = s.opacity;
		j[ "brightness" ]  = s.brightness;
		j[ "contrast" ]    = s.contrast;
		j[ "rgb" ]         = { s.red, s.green, s.blue };
		json slices        = json::array();
		for( const Slice& sl : s.slices )
		{
			json k;
			k[ "name" ]    = sl.name;
			k[ "enabled" ] = sl.enabled;
			k[ "type" ]    = sl.mask ? "mask" : sl.polygon ? "polygon" : "slice";
			auto writeContour = [ & ]() {
				json points = json::array();
				for( const ContourPoint& c : sl.contour )
					points.push_back( { c.p.x, c.p.y, c.in.x, c.in.y, c.out.x, c.out.y } );
				k[ "contour" ]   = points;
				k[ "pointMode" ] = PointModeName( sl.pointMode );
			};
			if( sl.mask )
			{
				writeContour();
				k[ "invert" ]  = sl.invert;
				k[ "feather" ] = sl.feather;
				k[ "opacity" ] = sl.opacity;
				slices.push_back( k );
				continue;
			}
			k[ "input" ]   = Points( sl.input );
			if( sl.polygon )
				k[ "output" ] = Points( sl.output );
			else
			{
				k[ "warp" ]    = WarpModeName( sl.warp );
				k[ "corners" ] = Points( std::vector< Vec2 >( sl.corners, sl.corners + 4 ) );
				if( sl.warp == WarpMode::Grid || sl.warp == WarpMode::Curved )
				{
					k[ "grid" ]     = Points( sl.grid );
					k[ "gridSize" ] = { sl.gridColumns, sl.gridRows };
				}
				if( sl.warp == WarpMode::Contour )
				{
					writeContour();
					k[ "contourCorners" ] = { sl.contourCorners[ 0 ], sl.contourCorners[ 1 ], sl.contourCorners[ 2 ], sl.contourCorners[ 3 ] };
				}
			}
			k[ "softEdge" ] = { { "left", sl.edge.left },   { "right", sl.edge.right },         { "top", sl.edge.top },        { "bottom", sl.edge.bottom },
				                { "power", sl.edge.power }, { "luminance", sl.edge.luminance }, { "gamma", sl.edge.gamma } };
			k[ "flip" ]     = { sl.flipHorizontal, sl.flipVertical };
			k[ "opacity" ]  = sl.opacity;
			slices.push_back( k );
		}
		j[ "slices" ] = slices;
		screens.push_back( j );
	}
	root[ "screens" ] = screens;
	return root.dump( 1 );
}

bool LoadAdvancedOutput( const std::string& text, AdvancedOutputSetup& out, std::string& error )
{
	json root = json::parse( text, nullptr, false );
	if( root.is_discarded() || !root.is_object() || root.value( "format", "" ) != "stlt-vj-advanced-output" )
	{
		error = "Not an STLT-VJ output setup";
		return false;
	}
	AdvancedOutputSetup setup;
	if( root.contains( "screens" ) && root[ "screens" ].is_array() )
		for( const json& j : root[ "screens" ] )
		{
			Screen s;
			s.name    = j.value( "name", s.name );
			s.enabled = j.value( "enabled", true );
			s.device  = ByName( j.value( "device", json() ), OutputDeviceName, OutputDevice::Virtual );
			s.display = j.value( "display", "" );
			if( j.contains( "size" ) && j[ "size" ].is_array() && j[ "size" ].size() == 2 )
			{
				s.width  = std::clamp( j[ "size" ][ 0 ].get< int >(), 16, 16384 );
				s.height = std::clamp( j[ "size" ][ 1 ].get< int >(), 16, 16384 );
			}
			s.opacity    = j.value( "opacity", 1.0f );
			s.brightness = j.value( "brightness", 0.0f );
			s.contrast   = j.value( "contrast", 0.0f );
			if( j.contains( "rgb" ) && j[ "rgb" ].is_array() && j[ "rgb" ].size() == 3 )
			{
				s.red   = j[ "rgb" ][ 0 ].get< float >();
				s.green = j[ "rgb" ][ 1 ].get< float >();
				s.blue  = j[ "rgb" ][ 2 ].get< float >();
			}
			if( j.contains( "slices" ) && j[ "slices" ].is_array() )
				for( const json& k : j[ "slices" ] )
				{
					Slice sl;
					sl.name    = k.value( "name", sl.name );
					sl.enabled = k.value( "enabled", true );
					std::string type = k.value( "type", "slice" );
					sl.polygon       = type == "polygon";
					sl.mask          = type == "mask";
					auto readContour = [ & ]() {
						if( k.contains( "contour" ) && k[ "contour" ].is_array() )
							for( const json& c : k[ "contour" ] )
								if( c.is_array() && c.size() == 6 )
									sl.contour.push_back( { { c[ 0 ].get< float >(), c[ 1 ].get< float >() },
									                        { c[ 2 ].get< float >(), c[ 3 ].get< float >() },
									                        { c[ 4 ].get< float >(), c[ 5 ].get< float >() } } );
						sl.pointMode = ByName( k.value( "pointMode", json() ), PointModeName, PointMode::Bezier );
					};
					if( sl.mask )
					{
						readContour();
						if( sl.contour.size() < 3 )
							continue;
						sl.invert  = k.value( "invert", false );
						sl.feather = std::max( k.value( "feather", 0.0f ), 0.0f );
						sl.opacity = k.value( "opacity", 1.0f );
						s.slices.push_back( std::move( sl ) );
						continue;
					}
					sl.input   = ReadPoints( k.value( "input", json() ) );
					if( sl.polygon )
					{
						sl.output = ReadPoints( k.value( "output", json() ) );
						if( sl.input.size() < 3 || sl.output.size() != sl.input.size() )
							continue;
					}
					else
					{
						if( sl.input.size() != 4 )
							continue;
						std::vector< Vec2 > c = ReadPoints( k.value( "corners", json() ) );
						for( int i = 0; i < 4 && i < (int)c.size(); ++i )
							sl.corners[ i ] = c[ i ];
						sl.warp = ByName( k.value( "warp", json() ), WarpModeName, WarpMode::Perspective );
						if( sl.warp == WarpMode::Contour )
						{
							readContour();
							if( k.contains( "contourCorners" ) && k[ "contourCorners" ].is_array() && k[ "contourCorners" ].size() == 4 )
								for( int i = 0; i < 4; ++i )
									sl.contourCorners[ i ] = k[ "contourCorners" ][ i ].get< int >();
							if( !ContourUsable( sl ) )
							{
								sl.warp = WarpMode::Perspective;
								sl.contour.clear();
							}
						}
						else if( sl.warp != WarpMode::Perspective )
						{
							sl.grid = ReadPoints( k.value( "grid", json() ) );
							if( k.contains( "gridSize" ) && k[ "gridSize" ].is_array() && k[ "gridSize" ].size() == 2 )
							{
								sl.gridColumns = k[ "gridSize" ][ 0 ].get< int >();
								sl.gridRows    = k[ "gridSize" ][ 1 ].get< int >();
							}
							if( sl.gridColumns < 2 || sl.gridRows < 2 || (int)sl.grid.size() != sl.gridColumns * sl.gridRows )
							{
								sl.warp = WarpMode::Perspective;
								sl.grid.clear();
							}
						}
					}
					if( k.contains( "softEdge" ) && k[ "softEdge" ].is_object() )
					{
						const json& e     = k[ "softEdge" ];
						sl.edge.left      = e.value( "left", 0.0f );
						sl.edge.right     = e.value( "right", 0.0f );
						sl.edge.top       = e.value( "top", 0.0f );
						sl.edge.bottom    = e.value( "bottom", 0.0f );
						sl.edge.power     = e.value( "power", 2.0f );
						sl.edge.luminance = e.value( "luminance", 0.5f );
						sl.edge.gamma     = e.value( "gamma", 2.2f );
					}
					if( k.contains( "flip" ) && k[ "flip" ].is_array() && k[ "flip" ].size() == 2 )
					{
						sl.flipHorizontal = k[ "flip" ][ 0 ].get< bool >();
						sl.flipVertical   = k[ "flip" ][ 1 ].get< bool >();
					}
					sl.opacity = k.value( "opacity", 1.0f );
					s.slices.push_back( std::move( sl ) );
				}
			setup.screens.push_back( std::move( s ) );
		}
	out = std::move( setup );
	return true;
}

// ---- Resolume AdvancedOutput.xml. Arena writes it in a fixed, regular shape, so plain scanning is enough
// (the same approach as the editor's reader in editor/src/advancedOutput.mjs).

namespace
{
std::string DecodeEntities( std::string t )
{
	const std::pair< const char*, const char* > entities[] = { { "&lt;", "<" }, { "&gt;", ">" }, { "&quot;", "\"" }, { "&apos;", "'" }, { "&amp;", "&" } };
	for( auto [ from, to ] : entities )
		for( size_t at = t.find( from ); at != std::string::npos; at = t.find( from, at + 1 ) )
			t.replace( at, std::strlen( from ), to );
	return t;
}

std::string Attribute( const std::string& tag, const std::string& name )
{
	std::string key = " " + name + "=\"";
	size_t at       = tag.find( key );
	if( at == std::string::npos )
		return {};
	at += key.size();
	size_t end = tag.find( '"', at );
	return end == std::string::npos ? std::string() : DecodeEntities( tag.substr( at, end - at ) );
}

struct Block
{
	std::string head, body;
};

// Every <tag ...>...</tag> in `xml`, in order (not nested in one another).
std::vector< Block > Blocks( const std::string& xml, const std::string& tag )
{
	std::vector< Block > result;
	size_t at = 0;
	for( ;; )
	{
		size_t open = xml.find( "<" + tag, at );
		if( open == std::string::npos )
			break;
		char next = open + 1 + tag.size() < xml.size() ? xml[ open + 1 + tag.size() ] : '\0';
		if( next != ' ' && next != '>' && next != '\t' && next != '\n' && next != '\r' && next != '/' )
		{
			at = open + 1;
			continue;
		}
		size_t headEnd = xml.find( '>', open );
		if( headEnd == std::string::npos )
			break;
		if( xml[ headEnd - 1 ] == '/' )
		{
			result.push_back( { xml.substr( open, headEnd + 1 - open ), {} } );
			at = headEnd + 1;
			continue;
		}
		size_t close = xml.find( "</" + tag + ">", headEnd );
		if( close == std::string::npos )
			break;
		result.push_back( { xml.substr( open, headEnd + 1 - open ), xml.substr( headEnd + 1, close - headEnd - 1 ) } );
		at = close + tag.size() + 3;
	}
	return result;
}

std::vector< Vec2 > Vertices( const std::string& xml )
{
	std::vector< Vec2 > points;
	for( size_t at = xml.find( "<v " ); at != std::string::npos; at = xml.find( "<v ", at + 1 ) )
	{
		size_t end      = xml.find( '>', at );
		std::string tag = xml.substr( at, end - at + 1 );
		points.push_back( { std::strtof( Attribute( tag, "x" ).c_str(), nullptr ), std::strtof( Attribute( tag, "y" ).c_str(), nullptr ) } );
	}
	return points;
}

std::string ParamValue( const std::string& body, const std::string& name )
{
	std::string key = "<Param name=\"" + name + "\"";
	size_t at       = body.find( key );
	if( at == std::string::npos )
		return {};
	size_t end = body.find( '>', at );
	return Attribute( body.substr( at, end - at + 1 ), "value" );
}

// The value of any kind of parameter (<Param>, <ParamRange>, <ParamChoice>...) called `name`, or "".
std::string AnyParamValue( const std::string& body, const std::string& name )
{
	std::string key = " name=\"" + name + "\"";
	for( size_t at = body.find( key ); at != std::string::npos; at = body.find( key, at + 1 ) )
	{
		size_t open = body.rfind( '<', at );
		if( open != std::string::npos && body.compare( open, 6, "<Param" ) == 0 )
		{
			size_t end = body.find( '>', at );
			return Attribute( body.substr( open, end - open + 1 ), "value" );
		}
	}
	return {};
}

bool IsTrue( const std::string& value )
{
	return value == "1" || value == "true" || value == "True";
}

// Resolume's warper: <BezierWarper controlWidth="W" controlHeight="H"> with its points in <vertices>, row by row
// from the top left. The points are a grid of anchors, or (in Bézier mode) the anchors with their handles in
// between, three to a cell; controlWidth and controlHeight count points or cells. Points in 0..1 are taken across
// the slice's output rectangle, others as screen pixels. The anchors become a grid, curved for Bézier.
void ImportWarp( const std::string& body, Slice& s )
{
	std::vector< Block > warpers = Blocks( body, "BezierWarper" );
	if( warpers.empty() )
		return;
	int w = std::atoi( Attribute( warpers[ 0 ].head, "controlWidth" ).c_str() );
	int h = std::atoi( Attribute( warpers[ 0 ].head, "controlHeight" ).c_str() );
	std::vector< Vec2 > v = Vertices( warpers[ 0 ].body );
	int columns = 0, rows = 0, stride = 1;
	for( int extra : { 0, 1 } )
	{
		int c = w + extra, r = h + extra;
		if( c < 2 || r < 2 || columns )
			continue;
		if( (int)v.size() == c * r )
			columns = c, rows = r;
		else if( (int)v.size() == ( 3 * c - 2 ) * ( 3 * r - 2 ) )
			columns = c, rows = r, stride = 3;
	}
	if( !columns || columns > 32 || rows > 32 )
		return;
	int fullColumns = stride == 3 ? 3 * columns - 2 : columns;
	bool unit       = std::all_of( v.begin(), v.end(), []( Vec2 p ) { return p.x > -0.01f && p.x < 1.01f && p.y > -0.01f && p.y < 1.01f; } );
	std::vector< Vec2 > grid( (size_t)columns * rows );
	bool moved = false;
	for( int r = 0; r < rows; ++r )
		for( int c = 0; c < columns; ++c )
		{
			Vec2 p  = v[ (size_t)( r * stride ) * fullColumns + c * stride ];
			Vec2 at = unit ? s.Map( p.x, p.y ) : p;
			Vec2 flat = s.Map( (float)c / ( columns - 1 ), (float)r / ( rows - 1 ) );
			moved |= std::hypot( at.x - flat.x, at.y - flat.y ) > 0.5f;
			grid[ (size_t)r * columns + c ] = at;
		}
	if( !moved )
		return; // an untouched warper: the plain output rectangle
	std::string mode = AnyParamValue( body, "Point Mode" );
	bool bezier      = stride == 3 || mode.find( "BEZIER" ) != std::string::npos || mode.find( "Bezier" ) != std::string::npos;
	s.gridColumns    = columns;
	s.gridRows       = rows;
	s.grid           = std::move( grid );
	s.warp           = bezier ? WarpMode::Curved : WarpMode::Grid;
}

// Resolume's soft edge, from its <SoftEdge> block or a "Soft Edge" parameter group: the four widths (shares of the
// slice, or pixels when above 1), power, luminance and gamma.
void ImportSoftEdge( const std::string& body, Slice& s )
{
	std::string region;
	std::vector< Block > blocks = Blocks( body, "SoftEdge" );
	if( !blocks.empty() )
		region = blocks[ 0 ].head + blocks[ 0 ].body;
	else
		for( const Block& b : Blocks( body, "Params" ) )
		{
			std::string name = Attribute( b.head, "name" );
			if( name.find( "Soft" ) != std::string::npos || name.find( "Blend" ) != std::string::npos )
				region = b.body;
		}
	if( region.empty() )
		return;
	std::string enabled = AnyParamValue( region, "Enabled" );
	if( !enabled.empty() && !IsTrue( enabled ) )
		return;
	float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
	for( Vec2 p : s.OutputOutline( 1 ) )
	{
		minX = std::min( minX, p.x );
		minY = std::min( minY, p.y );
		maxX = std::max( maxX, p.x );
		maxY = std::max( maxY, p.y );
	}
	auto width = [ & ]( const char* name, float size ) {
		std::string text = AnyParamValue( region, name );
		if( text.empty() )
			return 0.0f;
		float value = std::strtof( text.c_str(), nullptr );
		if( value > 1 )
			value /= std::max( size, 1.0f );
		return std::clamp( value, 0.0f, 0.5f );
	};
	s.edge.left   = width( "Left", maxX - minX );
	s.edge.right  = width( "Right", maxX - minX );
	s.edge.top    = width( "Top", maxY - minY );
	s.edge.bottom = width( "Bottom", maxY - minY );
	auto number = [ & ]( const char* name, float fallback, float lo, float hi ) {
		std::string text = AnyParamValue( region, name );
		return text.empty() ? fallback : std::clamp( std::strtof( text.c_str(), nullptr ), lo, hi );
	};
	s.edge.power     = number( "Power", s.edge.power, 0.5f, 6 );
	s.edge.luminance = number( "Luminance", s.edge.luminance, 0, 1 );
	s.edge.gamma     = number( "Gamma", s.edge.gamma, 0.5f, 3 );
}
}

bool ImportResolumeXml( const std::string& xml, AdvancedOutputSetup& out, std::string& error, int* compWidth, int* compHeight )
{
	std::vector< Block > size = Blocks( xml, "CurrentCompositionTextureSize" );
	float cw = size.empty() ? 0 : std::strtof( Attribute( size[ 0 ].head, "width" ).c_str(), nullptr );
	float ch = size.empty() ? 0 : std::strtof( Attribute( size[ 0 ].head, "height" ).c_str(), nullptr );
	if( !( cw > 0 && ch > 0 ) )
	{
		error = "No composition size: not an AdvancedOutput.xml";
		return false;
	}
	AdvancedOutputSetup setup;
	for( const Block& screenBlock : Blocks( xml, "Screen" ) )
	{
		Screen screen;
		std::string name = ParamValue( screenBlock.body, "Name" );
		screen.name      = name.empty() ? Attribute( screenBlock.head, "name" ) : name;
		if( screen.name.empty() )
			screen.name = "Screen " + std::to_string( setup.screens.size() + 1 );
		// The output device tag carries the screen size: <OutputDeviceVirtual ... width= height=>, <OutputDeviceDisplay ...>.
		for( size_t at = screenBlock.body.find( "<OutputDevice" ); at != std::string::npos; at = screenBlock.body.find( "<OutputDevice", at + 1 ) )
		{
			size_t end      = screenBlock.body.find( '>', at );
			std::string tag = screenBlock.body.substr( at, end - at + 1 );
			int w = std::atoi( Attribute( tag, "width" ).c_str() ), h = std::atoi( Attribute( tag, "height" ).c_str() );
			if( w > 0 && h > 0 )
			{
				screen.width  = w;
				screen.height = h;
				if( tag.find( "OutputDeviceVirtual" ) == std::string::npos )
					screen.device = OutputDevice::Window; // a real display in Resolume: pick it again here
				break;
			}
		}
		std::vector< Block > layers = Blocks( screenBlock.body, "layers" );
		if( !layers.empty() )
		{
			// Slices and polygons in file order; Arena lists the newest first, so reverse to keep its panel order.
			std::vector< Slice > slices;
			const std::string& body = layers[ 0 ].body;
			size_t at               = 0;
			for( ;; )
			{
				size_t slice = body.find( "<Slice ", at ), poly = body.find( "<Polygon ", at ), maskAt = body.find( "<Mask ", at );
				size_t open  = std::min( { slice, poly, maskAt } );
				if( open == std::string::npos )
					break;
				bool isPolygon  = open == poly;
				bool isMask     = open == maskAt;
				std::string tag = isMask ? "Mask" : isPolygon ? "Polygon" : "Slice";
				size_t close    = body.find( "</" + tag + ">", open );
				if( close == std::string::npos )
					break;
				std::string itemBody = body.substr( open, close - open );
				at                   = close + tag.size() + 3;

				Slice s;
				s.polygon         = isPolygon;
				std::string named = ParamValue( itemBody, "Name" );
				s.name            = named.empty() ? tag : named;
				std::string enabled = ParamValue( itemBody, "Enabled" );
				s.enabled           = enabled.empty() || IsTrue( enabled );
				if( isMask )
				{
					// A mask's outline: on the output, or else on the input, carried over to the screen.
					std::vector< Vec2 > outline;
					float sx = 1, sy = 1;
					for( const char* container : { "OutputContour", "Contour", "InputContour" } )
					{
						std::vector< Block > b = Blocks( itemBody, container );
						if( !b.empty() && ( outline = Vertices( b[ 0 ].body ) ).size() >= 3 )
						{
							if( std::string( container ) == "InputContour" )
								sx = screen.width / cw, sy = screen.height / ch;
							break;
						}
					}
					if( outline.size() < 3 )
						continue;
					s.mask      = true;
					s.pointMode = PointMode::Linear;
					for( Vec2 p : outline )
						s.contour.push_back( { { p.x * sx, p.y * sy }, {}, {} } );
					s.invert = IsTrue( AnyParamValue( itemBody, "Invert" ) );
					std::string feather = AnyParamValue( itemBody, "Feather" );
					s.feather           = feather.empty() ? 0 : std::max( std::strtof( feather.c_str(), nullptr ), 0.0f );
					slices.push_back( std::move( s ) );
					continue;
				}
				std::vector< Block > in  = Blocks( itemBody, isPolygon ? "InputContour" : "InputRect" );
				std::vector< Block > outB = Blocks( itemBody, isPolygon ? "OutputContour" : "OutputRect" );
				s.input                  = in.empty() ? std::vector< Vec2 >() : Vertices( in[ 0 ].body );
				std::vector< Vec2 > outP = outB.empty() ? std::vector< Vec2 >() : Vertices( outB[ 0 ].body );
				if( isPolygon )
				{
					if( s.input.size() < 3 )
						continue;
					if( outP.size() == s.input.size() )
						s.output = outP;
					else
						s.MatchOutputToInput( cw, ch, (float)screen.width, (float)screen.height );
				}
				else
				{
					if( s.input.size() != 4 )
						continue;
					if( outP.size() == 4 )
						std::copy( outP.begin(), outP.end(), s.corners );
					else
						s.MatchOutputToInput( cw, ch, (float)screen.width, (float)screen.height );
					ImportWarp( itemBody, s );
				}
				ImportSoftEdge( itemBody, s );
				slices.push_back( std::move( s ) );
			}
			std::reverse( slices.begin(), slices.end() );
			screen.slices = std::move( slices );
		}
		setup.screens.push_back( std::move( screen ) );
	}
	if( setup.screens.empty() )
	{
		error = "No screens in the file";
		return false;
	}
	if( compWidth )
		*compWidth = (int)cw;
	if( compHeight )
		*compHeight = (int)ch;
	out = std::move( setup );
	return true;
}
}
