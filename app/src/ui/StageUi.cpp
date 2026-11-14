#include "ui/StageUi.h"
#include <imgui.h>
#include <imgui_stdlib.h>
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include "App.h"
#include "output/Stage.h"
#include "ui/AdvancedOutputUi.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

namespace stltvj
{
namespace
{
constexpr float kPi = 3.14159265f;

Vec3 Add( Vec3 a, Vec3 b ) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
Vec3 Sub( Vec3 a, Vec3 b ) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
Vec3 Mul( Vec3 a, float s ) { return { a.x * s, a.y * s, a.z * s }; }
float Dot( Vec3 a, Vec3 b ) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Cross( Vec3 a, Vec3 b ) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
Vec3 Normalise( Vec3 a )
{
	float l = std::sqrt( Dot( a, a ) );
	return l > 1e-9f ? Mul( a, 1 / l ) : Vec3{ 0, 0, 0 };
}

enum class ViewKind
{
	Orbit,
	Top,
	Front,
	Through, // through the selected projector's lens
};

enum class Colouring
{
	Projectors,
	Brightness,
	Sharpness,
};

enum class Selected
{
	None,
	Surface,
	Projector,
};

struct StageEditor
{
	Selected selected = Selected::None;
	int index         = -1;
	ViewKind view     = ViewKind::Orbit;
	int through       = 0; // the projector the Through view looks through
	Colouring colouring = Colouring::Projectors;
	// The orbit camera.
	Vec3 target   = { 0, 5, 4 };
	float yaw     = -28, pitch = 14, distance = 42;
	// Top and front: centre and metres across.
	Vec3 orthoCentre = { 0, 4, 8 };
	float orthoSpan  = 50;
	// A projector dragged in the view.
	bool dragging = false;
	Vec3 dragOffset;
	bool detached = false;
} stage;

// A camera: the world to the view's pixels.
struct Camera
{
	Vec3 eye, forward, right, up;
	bool ortho  = false;
	float scale = 1;         // pixels per metre at one metre (perspective) or per metre (ortho)
	float shiftX = 0, shiftY = 0; // the lens shift (through a projector)
	ImVec2 centre;
	Vec3 ToCamera( Vec3 p ) const
	{
		Vec3 d = Sub( p, eye );
		return { Dot( d, right ), Dot( d, up ), Dot( d, forward ) };
	}
	ImVec2 Project( Vec3 c ) const
	{
		if( ortho )
			return ImVec2( centre.x + c.x * scale, centre.y - c.y * scale );
		return ImVec2( centre.x + ( c.x / c.z - shiftX ) * scale, centre.y - ( c.y / c.z - shiftY ) * scale );
	}
	// The ray under a pixel of the view.
	void Ray( ImVec2 at, Vec3& origin, Vec3& direction ) const
	{
		float x = ( at.x - centre.x ) / scale, y = -( at.y - centre.y ) / scale;
		if( ortho )
		{
			origin    = Add( Add( eye, Mul( right, x ) ), Mul( up, y ) );
			direction = forward;
			return;
		}
		origin    = eye;
		direction = Normalise( Add( Add( forward, Mul( right, x + shiftX ) ), Mul( up, y + shiftY ) ) );
	}
};

Camera MakeCamera( const Stage& st, ImVec2 pos, ImVec2 size )
{
	Camera c;
	c.centre = ImVec2( pos.x + size.x / 2, pos.y + size.y / 2 );
	switch( stage.view )
	{
	case ViewKind::Top:
		c.ortho   = true;
		c.forward = { 0, -1, 0 };
		c.right   = { 1, 0, 0 };
		c.up      = { 0, 0, -1 };
		c.eye     = Add( stage.orthoCentre, { 0, 500, 0 } );
		c.scale   = std::min( size.x, size.y ) / std::max( 1.0f, stage.orthoSpan );
		return c;
	case ViewKind::Front:
		c.ortho   = true;
		c.forward = { 0, 0, -1 };
		c.right   = { 1, 0, 0 };
		c.up      = { 0, 1, 0 };
		c.eye     = Add( stage.orthoCentre, { 0, 0, 500 } );
		c.scale   = std::min( size.x, size.y ) / std::max( 1.0f, stage.orthoSpan );
		return c;
	case ViewKind::Through:
		if( stage.through >= 0 && stage.through < (int)st.projectors.size() )
		{
			const StageProjector& p = st.projectors[ stage.through ];
			ProjectorFrame f        = FrameOf( p );
			c.eye     = f.position;
			c.forward = f.forward;
			c.right   = f.right;
			c.up      = f.up;
			// The whole picture fits the view.
			float aspect = (float)p.width / std::max( 1, p.height );
			float w = size.x, h = size.x / aspect;
			if( h > size.y )
			{
				h = size.y;
				w = h * aspect;
			}
			c.scale  = w / 2 / f.halfWidth;
			c.shiftX = f.centreX;
			c.shiftY = f.centreY;
			return c;
		}
		[[fallthrough]];
	case ViewKind::Orbit:
	default:
	{
		float yaw = stage.yaw * kPi / 180, pitch = stage.pitch * kPi / 180;
		Vec3 back = { std::sin( yaw ) * std::cos( pitch ), std::sin( pitch ), std::cos( yaw ) * std::cos( pitch ) };
		c.eye     = Add( stage.target, Mul( back, stage.distance ) );
		c.forward = Mul( back, -1 );
		c.right   = Normalise( Cross( c.forward, { 0, 1, 0 } ) );
		c.up      = Cross( c.right, c.forward );
		c.scale   = size.y / 2 / std::tan( 25 * kPi / 180 ); // 50° high
		return c;
	}
	}
}

// A polygon to draw, sorted far to near.
struct Shape
{
	float depth = 0;
	std::vector< ImVec2 > points;
	ImU32 fill = 0, line = 0;
	float thickness = 1;
	bool dashed     = false;
};

// A polygon in the world, cut where it passes behind the camera, in the view's pixels.
bool Flatten( const Camera& c, const std::vector< Vec3 >& world, std::vector< ImVec2 >& out, float& depth )
{
	std::vector< Vec3 > cam;
	for( Vec3 p : world )
		cam.push_back( c.ToCamera( p ) );
	if( !c.ortho )
	{
		const float nearZ = 0.05f; // not "near": a macro in Windows' headers
		std::vector< Vec3 > clipped;
		for( size_t i = 0, n = cam.size(); i < n; ++i )
		{
			Vec3 a = cam[ i ], b = cam[ ( i + 1 ) % n ];
			if( a.z >= nearZ )
				clipped.push_back( a );
			if( ( a.z >= nearZ ) != ( b.z >= nearZ ) )
			{
				float t = ( nearZ - a.z ) / ( b.z - a.z );
				clipped.push_back( Add( a, Mul( Sub( b, a ), t ) ) );
			}
		}
		cam = clipped;
	}
	if( cam.size() < 2 )
		return false;
	out.clear();
	depth = 0;
	for( Vec3 p : cam )
	{
		out.push_back( c.Project( p ) );
		depth += p.z;
	}
	depth /= cam.size();
	return true;
}

void DrawDashedPolyline( ImDrawList* draw, const std::vector< ImVec2 >& points, ImU32 colour, float thickness, float dash )
{
	for( size_t i = 0, n = points.size(); i < n; ++i )
	{
		ImVec2 a = points[ i ], b = points[ ( i + 1 ) % n ];
		float len = std::hypot( b.x - a.x, b.y - a.y );
		for( float s = 0; s < len; s += dash * 2 )
		{
			float e = std::min( len, s + dash );
			draw->AddLine( ImVec2( a.x + ( b.x - a.x ) * s / len, a.y + ( b.y - a.y ) * s / len ), ImVec2( a.x + ( b.x - a.x ) * e / len, a.y + ( b.y - a.y ) * e / len ),
			               colour, thickness );
		}
	}
}

ImU32 WithAlpha( ImU32 colour, int alpha ) { return ( colour & 0x00FFFFFF ) | ( (ImU32)alpha << 24 ); }

// A value on a ramp from red (poor) through amber to green (good).
ImU32 Ramp( float t, int alpha )
{
	t = std::clamp( t, 0.0f, 1.0f );
	int r = t < 0.5f ? 235 : (int)( 235 - ( t - 0.5f ) * 2 * 140 );
	int g = t < 0.5f ? (int)( 70 + t * 2 * 120 ) : (int)( 190 + ( t - 0.5f ) * 2 * 20 );
	return IM_COL32( r, g, 70, alpha );
}

std::string SurfaceLabel( const Stage& st, const StageFace& face )
{
	static const char* sides[] = { "front", "back", "left", "right", "top", "bottom" };
	if( face.surface < 0 || face.surface >= (int)st.surfaces.size() )
		return "?";
	const StageSurface& s = st.surfaces[ face.surface ];
	return s.kind == StageSurface::Kind::Box ? s.name + " (" + sides[ face.face % 6 ] + ")" : s.name;
}

// Where a ray meets a face first (its distance; -1: it does not).
float HitFace( const StageFace& f, Vec3 origin, Vec3 direction )
{
	Vec3 n      = Normalise( Cross( f.ex, f.ey ) );
	float along = Dot( n, direction );
	if( std::abs( along ) < 1e-6f )
		return -1;
	float t = Dot( n, Sub( f.origin, origin ) ) / along;
	if( t <= 0 )
		return -1;
	Vec3 d  = Sub( Add( origin, Mul( direction, t ) ), f.origin );
	float x = Dot( d, f.ex ), y = Dot( d, f.ey );
	return x >= 0 && x <= f.width && y >= 0 && y <= f.height ? t : -1;
}

// Aims a projector at a point.
void AimAt( StageProjector& p, Vec3 point )
{
	Vec3 d = Sub( point, p.position );
	float flat = std::sqrt( d.x * d.x + d.z * d.z );
	p.pan  = std::atan2( d.x, -d.z ) * 180 / kPi;
	p.tilt = std::atan2( d.y, flat ) * 180 / kPi;
}

Vec3 SurfaceCentre( const StageSurface& s ) { return s.position; }

// A projector's colour: its screen's (the same as in steps 1 and 2), or its own until it has one.
ImU32 ProjectorColour( const AdvancedOutputSetup& setup, int index )
{
	const StageProjector& p = setup.stage.projectors[ index ];
	for( size_t s = 0; s < setup.screens.size(); ++s )
		if( !p.screen.empty() && setup.screens[ s ].id == p.screen )
			return ScreenColour( (int)s );
	return ScreenColour( index );
}

// ---- the 3D view

void DrawView( App& app, AdvancedOutputSetup& setup, ImVec2 size )
{
	Stage& st      = setup.stage;
	ImDrawList* draw = ImGui::GetWindowDrawList();
	ImVec2 pos     = ImGui::GetCursorScreenPos();
	ImGui::InvisibleButton( "##stageView", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle );
	bool hovered = ImGui::IsItemHovered();
	bool active  = ImGui::IsItemActive();
	ImGuiIO& io  = ImGui::GetIO();
	if( stage.view == ViewKind::Through && ( stage.through < 0 || stage.through >= (int)st.projectors.size() ) )
		stage.view = ViewKind::Orbit;
	Camera cam = MakeCamera( st, pos, size );
	std::vector< StageFace > faces = Faces( st );
	std::vector< std::vector< Lighting > > lighting;
	for( const StageProjector& p : st.projectors )
		lighting.push_back( LightingOf( faces, p ) );

	draw->PushClipRect( pos, ImVec2( pos.x + size.x, pos.y + size.y ), true );
	draw->AddRectFilled( pos, ImVec2( pos.x + size.x, pos.y + size.y ), IM_COL32( 10, 9, 8, 255 ) );

	// The floor's grid, every 2 m (not through a lens: there the surfaces say enough).
	if( stage.view != ViewKind::Through && stage.view != ViewKind::Front )
		for( int i = -30; i <= 30; i += 2 )
		{
			std::vector< ImVec2 > line;
			float depth;
			if( Flatten( cam, { Vec3{ (float)i, 0, -20 }, Vec3{ (float)i, 0, 50 } }, line, depth ) && line.size() >= 2 )
				draw->AddLine( line[ 0 ], line[ 1 ], i == 0 ? IM_COL32( 44, 40, 36, 255 ) : IM_COL32( 29, 27, 24, 255 ) );
			if( Flatten( cam, { Vec3{ -30, 0, (float)i + 15 }, Vec3{ 30, 0, (float)i + 15 } }, line, depth ) && line.size() >= 2 )
				draw->AddLine( line[ 0 ], line[ 1 ], IM_COL32( 29, 27, 24, 255 ) );
		}

	std::vector< Shape > shapes;
	// The faces.
	for( int i = 0; i < (int)faces.size(); ++i )
	{
		const StageFace& f = faces[ i ];
		Shape s;
		if( !Flatten( cam, { f.Corner( 0 ), f.Corner( 1 ), f.Corner( 2 ), f.Corner( 3 ) }, s.points, s.depth ) || s.points.size() < 3 )
			continue;
		bool sel = stage.selected == Selected::Surface && stage.index == f.surface;
		// Shaded by which way it faces: walls lighter than floors, so the shapes read at a glance.
		Vec3 n      = f.Normal();
		float light = 0.55f + 0.45f * std::abs( Dot( n, Normalise( { -0.4f, 0.35f, 0.85f } ) ) ) * ( 1 - 0.6f * std::abs( n.y ) );
		int grey    = (int)( 22 + 30 * light );
		s.fill      = std::abs( n.y ) > 0.9f ? IM_COL32( grey - 6, grey - 7, grey - 9, 170 ) : IM_COL32( grey, grey - 2, grey - 5, 235 );
		s.line      = sel ? theme::Accent : IM_COL32( 101, 95, 87, 255 );
		s.thickness = sel ? Ui( 2 ) : Ui( 1 );
		shapes.push_back( s );
		// What each projector lights on it, just in front of it.
		for( int p = 0; p < (int)lighting.size(); ++p )
			for( const Lighting& l : lighting[ p ] )
			{
				if( l.face != i )
					continue;
				Shape lit;
				if( !Flatten( cam, l.polygon, lit.points, lit.depth ) || lit.points.size() < 3 )
					continue;
				lit.depth = s.depth - 0.001f * ( p + 1 );
				ImU32 colour = ProjectorColour( setup, p );
				lit.fill = stage.colouring == Colouring::Brightness  ? Ramp( l.lux / 500, 110 )
				           : stage.colouring == Colouring::Sharpness ? Ramp( l.pixelsPerMetre / 150, 110 )
				                                                     : WithAlpha( colour, 64 );
				lit.line      = colour;
				lit.thickness = Ui( 1.5f );
				shapes.push_back( lit );
			}
		// Where two projectors overlap: the zone to blend.
		for( int a = 0; a < (int)lighting.size(); ++a )
			for( int b = a + 1; b < (int)lighting.size(); ++b )
				for( const Lighting& la : lighting[ a ] )
					for( const Lighting& lb : lighting[ b ] )
						if( la.face == i && lb.face == i )
						{
							std::vector< Vec3 > shared = Overlap( f, la.polygon, lb.polygon );
							Shape o;
							if( shared.empty() || !Flatten( cam, shared, o.points, o.depth ) || o.points.size() < 3 )
								continue;
							o.depth  = s.depth - 0.01f;
							o.fill   = IM_COL32( 255, 255, 255, 46 );
							o.line   = IM_COL32( 255, 255, 255, 200 );
							o.dashed = true;
							shapes.push_back( o );
						}
	}
	std::stable_sort( shapes.begin(), shapes.end(), []( const Shape& a, const Shape& b ) { return a.depth > b.depth; } );
	for( const Shape& s : shapes )
	{
		if( s.fill )
			draw->AddConvexPolyFilled( s.points.data(), (int)s.points.size(), s.fill );
		if( s.dashed )
			DrawDashedPolyline( draw, s.points, s.line, Ui( 1 ), Ui( 4 ) );
		else if( s.line )
			draw->AddPolyline( s.points.data(), (int)s.points.size(), s.line, ImDrawFlags_Closed, s.thickness );
	}

	// The projectors: the beam's edges to where they land, the body, its number.
	std::vector< ImVec2 > bodies( st.projectors.size(), ImVec2( -1e9f, -1e9f ) );
	for( int p = 0; p < (int)st.projectors.size(); ++p )
	{
		const StageProjector& pr = st.projectors[ p ];
		ProjectorFrame f         = FrameOf( pr );
		ImU32 colour             = ProjectorColour( setup, p );
		bool through             = stage.view == ViewKind::Through && stage.through == p;
		if( !through )
			for( float u : { -1.0f, 1.0f } )
				for( float v : { -1.0f, 1.0f } )
				{
					Vec3 dir  = Normalise( RayThrough( f, u, v ) );
					float hit = -1;
					for( const StageFace& face : faces )
					{
						float t = HitFace( face, f.position, dir );
						if( t > 0 && ( hit < 0 || t < hit ) )
							hit = t;
					}
					std::vector< ImVec2 > line;
					float depth;
					if( Flatten( cam, { f.position, Add( f.position, Mul( dir, hit > 0 ? hit : 12 ) ) }, line, depth ) && line.size() >= 2 )
						draw->AddLine( line[ 0 ], line[ 1 ], WithAlpha( colour, hit > 0 ? 150 : 60 ), Ui( 1 ) );
				}
		Vec3 c = cam.ToCamera( pr.position );
		if( through || ( !cam.ortho && c.z < 0.1f ) )
			continue;
		ImVec2 at = cam.Project( c );
		bodies[ p ] = at;
		bool sel    = stage.selected == Selected::Projector && stage.index == p;
		float w = Ui( 13 ), h = Ui( 8 );
		draw->AddRectFilled( ImVec2( at.x - w, at.y - h ), ImVec2( at.x + w, at.y + h ), sel ? IM_COL32( 42, 33, 22, 255 ) : theme::Raised );
		draw->AddRect( ImVec2( at.x - w, at.y - h ), ImVec2( at.x + w, at.y + h ), colour, 0, 0, sel ? Ui( 2 ) : Ui( 1.2f ) );
		char number[ 16 ];
		std::snprintf( number, sizeof( number ), "%d", p + 1 );
		ImVec2 t = ImGui::CalcTextSize( number );
		draw->AddText( ImVec2( at.x - t.x / 2, at.y - t.y / 2 ), colour, number );
	}

	// The numbers on each lit part: pixels per metre and lux.
	ImFont* small = Fonts().small;
	for( int p = 0; p < (int)lighting.size(); ++p )
		for( const Lighting& l : lighting[ p ] )
		{
			Vec3 mid;
			for( Vec3 v : l.polygon )
				mid = Add( mid, v );
			mid = Mul( mid, 1.0f / l.polygon.size() );
			Vec3 c = cam.ToCamera( mid );
			if( !cam.ortho && c.z < 0.1f )
				continue;
			ImVec2 at = cam.Project( c );
			char text[ 48 ];
			std::snprintf( text, sizeof( text ), "%.0f px/m  %.0f lx", l.pixelsPerMetre, l.lux );
			ImVec2 t = small->CalcTextSizeA( small->FontSize, FLT_MAX, 0, text );
			at.y += Ui( 14 ) * p;
			draw->AddRectFilled( ImVec2( at.x - t.x / 2 - Ui( 4 ), at.y - Ui( 1 ) ), ImVec2( at.x + t.x / 2 + Ui( 4 ), at.y + t.y + Ui( 1 ) ), IM_COL32( 13, 12, 11, 190 ) );
			draw->AddText( small, small->FontSize, ImVec2( at.x - t.x / 2, at.y ), ProjectorColour( setup, p ), text );
		}

	// Through a lens: the picture's frame.
	if( stage.view == ViewKind::Through )
	{
		const StageProjector& p = st.projectors[ stage.through ];
		ProjectorFrame f        = FrameOf( p );
		ImVec2 a = cam.Project( { f.centreX - f.halfWidth, f.centreY + f.halfHeight, 1 } ), b = cam.Project( { f.centreX + f.halfWidth, f.centreY - f.halfHeight, 1 } );
		draw->AddRect( a, b, ProjectorColour( setup, stage.through ), 0, 0, Ui( 2 ) );
		char text[ 96 ];
		std::snprintf( text, sizeof( text ), "Through %d %s: %d x %d", stage.through + 1, p.name.c_str(), p.width, p.height );
		draw->AddText( ImVec2( a.x + Ui( 8 ), b.y - ImGui::GetTextLineHeight() - Ui( 6 ) ), ProjectorColour( setup, stage.through ), text );
	}
	draw->PopClipRect();

	// ---- the mouse: pick, drag a projector, orbit, pan, zoom
	if( ImGui::IsItemActivated() && ImGui::IsMouseClicked( ImGuiMouseButton_Left ) )
	{
		int picked = -1;
		for( int p = 0; p < (int)bodies.size(); ++p )
			if( std::abs( io.MousePos.x - bodies[ p ].x ) < Ui( 15 ) && std::abs( io.MousePos.y - bodies[ p ].y ) < Ui( 10 ) )
				picked = p;
		if( picked >= 0 )
		{
			stage.selected = Selected::Projector;
			stage.index    = picked;
			stage.dragging = true;
			stage.dragOffset = {};
		}
		else
		{
			Vec3 o, d;
			cam.Ray( io.MousePos, o, d );
			float best = -1;
			int face   = -1;
			for( int i = 0; i < (int)faces.size(); ++i )
			{
				float t = HitFace( faces[ i ], o, d );
				if( t > 0 && ( best < 0 || t < best ) )
				{
					best = t;
					face = i;
				}
			}
			if( face >= 0 )
			{
				stage.selected = Selected::Surface;
				stage.index    = faces[ face ].surface;
			}
			else if( io.MouseClickedCount[ 0 ] == 1 )
			{
				stage.selected = Selected::None;
				stage.index    = -1;
			}
		}
	}
	if( !ImGui::IsMouseDown( ImGuiMouseButton_Left ) )
		stage.dragging = false;
	if( active && stage.dragging && stage.selected == Selected::Projector && stage.index >= 0 && stage.index < (int)st.projectors.size()
	    && stage.view != ViewKind::Through && ( io.MouseDelta.x != 0 || io.MouseDelta.y != 0 ) )
	{
		StageProjector& p = st.projectors[ stage.index ];
		if( io.KeyAlt || stage.view == ViewKind::Front )
		{
			// Up and down (Front: also sideways).
			float metres = stage.view == ViewKind::Orbit ? stage.distance / cam.scale : 1 / cam.scale;
			p.position.y = std::max( 0.0f, p.position.y - io.MouseDelta.y * metres );
			if( stage.view == ViewKind::Front )
				p.position.x += io.MouseDelta.x * metres;
		}
		else
		{
			// Along the floor, at its height.
			Vec3 o, d;
			cam.Ray( io.MousePos, o, d );
			if( std::abs( d.y ) > 1e-4f )
			{
				float t = ( p.position.y - o.y ) / d.y;
				if( t > 0 )
				{
					p.position.x = o.x + d.x * t;
					p.position.z = o.z + d.z * t;
				}
			}
		}
	}
	bool orbiting = active && !stage.dragging
	                && ( ImGui::IsMouseDragging( ImGuiMouseButton_Left, 2 ) || ImGui::IsMouseDragging( ImGuiMouseButton_Right, 2 ) );
	bool panning = active && ImGui::IsMouseDragging( ImGuiMouseButton_Middle, 2 );
	if( orbiting && io.KeyShift )
	{
		panning  = true;
		orbiting = false;
	}
	if( stage.view == ViewKind::Orbit )
	{
		if( orbiting )
		{
			stage.yaw -= io.MouseDelta.x * 0.3f;
			stage.pitch = std::clamp( stage.pitch + io.MouseDelta.y * 0.3f, -5.0f, 89.0f );
		}
		if( panning )
		{
			float metres = stage.distance / cam.scale;
			stage.target = Add( stage.target, Add( Mul( cam.right, -io.MouseDelta.x * metres ), Mul( cam.up, io.MouseDelta.y * metres ) ) );
		}
		if( hovered && io.MouseWheel != 0 )
			stage.distance = std::clamp( stage.distance * std::pow( 0.88f, io.MouseWheel ), 2.0f, 400.0f );
	}
	else if( stage.view != ViewKind::Through )
	{
		if( orbiting || panning )
			stage.orthoCentre = Add( stage.orthoCentre, Add( Mul( cam.right, -io.MouseDelta.x / cam.scale ), Mul( cam.up, io.MouseDelta.y / cam.scale ) ) );
		if( hovered && io.MouseWheel != 0 )
			stage.orthoSpan = std::clamp( stage.orthoSpan * std::pow( 0.88f, io.MouseWheel ), 2.0f, 1000.0f );
	}
	// How to move around, in a corner (not a tooltip over the picture).
	{
		const char* hint = stage.view == ViewKind::Through ? "What this projector lights, as it will send it out"
		                   : stage.view == ViewKind::Orbit ? "Drag: turn around  \xC2\xB7  Shift or middle button: slide  \xC2\xB7  Wheel: closer  \xC2\xB7  Drag a projector: move it (Alt: up, down)"
		                                                   : "Drag: slide  \xC2\xB7  Wheel: closer  \xC2\xB7  Drag a projector: move it";
		ImFont* font = Fonts().small;
		draw->AddText( font, font->FontSize, ImVec2( pos.x + Ui( 8 ), pos.y + size.y - font->FontSize - Ui( 6 ) ), theme::TextFaint, hint );
	}
	(void)app;
}

// ---- the left: surfaces and projectors

void DrawLists( App& app, AdvancedOutputSetup& setup )
{
	Stage& st = setup.stage;
	auto heading = []( const char* text ) {
		ImGui::PushFont( Fonts().small );
		ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( theme::TextDim ), "%s", text );
		ImGui::PopFont();
	};
	heading( "SPACE" );
	for( int i = 0; i < (int)st.surfaces.size(); ++i )
	{
		StageSurface& s = st.surfaces[ i ];
		ImGui::PushID( i );
		char size[ 48 ];
		if( s.kind == StageSurface::Kind::Box )
			std::snprintf( size, sizeof( size ), "%.4g x %.4g x %.4g m", s.size.x, s.size.y, s.size.z );
		else
			std::snprintf( size, sizeof( size ), "%.4g x %.4g m", s.size.x, s.size.y );
		if( ImGui::Selectable( s.name.c_str(), stage.selected == Selected::Surface && stage.index == i ) )
		{
			stage.selected = Selected::Surface;
			stage.index    = i;
		}
		ImGui::SameLine( ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize( size ).x + ImGui::GetCursorPosX() - Ui( 4 ) );
		ImGui::TextDisabled( "%s", size );
		ImGui::PopID();
	}
	if( ImGui::Button( "+ Surface" ) )
	{
		StageSurface s;
		s.name     = "Surface " + std::to_string( st.surfaces.size() + 1 );
		s.position = { 0, 2.5f, 0 };
		s.size     = { 8, 5, 0 };
		st.surfaces.push_back( s );
		setup.EnsureIds();
		stage.selected = Selected::Surface;
		stage.index    = (int)st.surfaces.size() - 1;
	}
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "A flat rectangle: a façade, a screen, a wall, a floor" );
	ImGui::SameLine();
	if( ImGui::Button( "+ Block" ) )
	{
		StageSurface s;
		s.name     = "Block " + std::to_string( st.surfaces.size() + 1 );
		s.kind     = StageSurface::Kind::Box;
		s.position = { 0, 1, 4 };
		s.size     = { 2, 2, 2 };
		st.surfaces.push_back( s );
		setup.EnsureIds();
		stage.selected = Selected::Surface;
		stage.index    = (int)st.surfaces.size() - 1;
	}
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "A box: a cube, a column, a stage riser; its six faces catch the light" );
	ImGui::BeginDisabled();
	ImGui::TextWrapped( "Import a 3D model (LiDAR): next" );
	ImGui::EndDisabled();

	ImGui::Spacing();
	ImGui::Spacing();
	heading( "PROJECTORS" );
	for( int i = 0; i < (int)st.projectors.size(); ++i )
	{
		StageProjector& p = st.projectors[ i ];
		ImGui::PushID( 1000 + i );
		ImVec2 at = ImGui::GetCursorScreenPos();
		float h   = ImGui::GetTextLineHeight();
		std::string number = std::to_string( i + 1 );
		float bw = TextBadge( ImGui::GetWindowDrawList(), ImVec2( at.x, at.y + Ui( 1 ) ), number.c_str(), ProjectorColour( setup, i ), IM_COL32( 17, 17, 17, 255 ), h );
		ImGui::SetCursorScreenPos( ImVec2( at.x + bw + Ui( 6 ), at.y ) );
		if( ImGui::Selectable( p.name.c_str(), stage.selected == Selected::Projector && stage.index == i ) )
		{
			stage.selected = Selected::Projector;
			stage.index    = i;
		}
		std::string to = "\xE2\x86\x92 -";
		for( size_t s = 0; s < setup.screens.size(); ++s )
			if( !p.screen.empty() && setup.screens[ s ].id == p.screen )
				to = "\xE2\x86\x92 Screen " + std::to_string( s + 1 );
		ImGui::SameLine( ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize( to.c_str() ).x + ImGui::GetCursorPosX() - Ui( 4 ) );
		ImGui::TextDisabled( "%s", to.c_str() );
		ImGui::PopID();
	}
	if( ImGui::Button( "+ Projector" ) )
	{
		StageProjector p;
		p.name = "Projector " + std::to_string( st.projectors.size() + 1 );
		// In front of the first surface, aimed at it.
		Vec3 aim = st.surfaces.empty() ? Vec3{ 0, 3, 0 } : SurfaceCentre( st.surfaces[ 0 ] );
		p.position = { aim.x + 4.0f * ( (int)st.projectors.size() % 2 ? 1 : -1 ) * (float)( ( st.projectors.size() + 1 ) / 2 ), 2, aim.z + 20 };
		AimAt( p, aim );
		st.projectors.push_back( p );
		setup.EnsureIds();
		stage.selected = Selected::Projector;
		stage.index    = (int)st.projectors.size() - 1;
	}
	ImGui::Spacing();
	ImGui::PushTextWrapPos( 0 );
	ImGui::TextDisabled( "Drag a projector in the view to move it (Alt: up and down). Its settings, and what it lights, are on the right." );
	ImGui::PopTextWrapPos();
	(void)app;
}

// ---- the right: the selected surface or projector

bool Row( const char* label )
{
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex( 0 );
	ImGui::AlignTextToFramePadding();
	ImGui::TextUnformatted( label );
	ImGui::TableSetColumnIndex( 1 );
	ImGui::SetNextItemWidth( -FLT_MIN );
	return true;
}

void Section( const char* title, const char* hint = nullptr )
{
	ImGui::Spacing();
	ImVec2 a = ImGui::GetCursorScreenPos();
	float w  = ImGui::GetContentRegionAvail().x, h = ImGui::GetFrameHeight();
	ImGui::GetWindowDrawList()->AddRectFilled( a, ImVec2( a.x + w, a.y + h ), theme::Raised );
	ImGui::SetCursorScreenPos( ImVec2( a.x + Ui( 8 ), a.y + ( h - ImGui::GetTextLineHeight() ) / 2 ) );
	ImGui::PushFont( Fonts().bold );
	ImGui::TextUnformatted( title );
	ImGui::PopFont();
	if( hint )
	{
		ImGui::SameLine();
		ImGui::TextDisabled( "%s", hint );
	}
	ImGui::SetCursorScreenPos( ImVec2( a.x, a.y + h + Ui( 4 ) ) );
}

void DrawSurfaceProps( AdvancedOutputSetup& setup, int index )
{
	Stage& st       = setup.stage;
	StageSurface& s = st.surfaces[ index ];
	ImGui::PushFont( Fonts().bold );
	ImGui::TextUnformatted( s.name.c_str() );
	ImGui::PopFont();
	ImGui::SameLine();
	ImGui::TextDisabled( s.kind == StageSurface::Kind::Box ? "block" : "surface" );
	Section( "Where", "metres, degrees" );
	if( ImGui::BeginTable( "##surface", 2, ImGuiTableFlags_SizingStretchProp ) )
	{
		ImGui::TableSetupColumn( "l", ImGuiTableColumnFlags_WidthFixed, Ui( 80 ) );
		ImGui::TableSetupColumn( "v", ImGuiTableColumnFlags_WidthStretch );
		Row( "Name" );
		ImGui::InputText( "##name", &s.name );
		Row( "Centre" );
		ImGui::DragFloat3( "##pos", &s.position.x, 0.05f, -1000, 1000, "%.2f" );
		Row( s.kind == StageSurface::Kind::Box ? "W H D" : "Width, height" );
		if( s.kind == StageSurface::Kind::Box )
			ImGui::DragFloat3( "##size", &s.size.x, 0.05f, 0.01f, 1000, "%.2f" );
		else
			ImGui::DragFloat2( "##size", &s.size.x, 0.05f, 0.01f, 1000, "%.2f" );
		Row( "Turn" );
		ImGui::DragFloat3( "##rot", &s.rotation.x, 0.5f, -360, 360, "%.1f\xC2\xB0" );
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex( 1 );
		ImGui::TextDisabled( "turn  \xC2\xB7  tilt (-90: flat, facing up)  \xC2\xB7  roll" );
		ImGui::EndTable();
	}
	s.size = { std::max( 0.01f, s.size.x ), std::max( 0.01f, s.size.y ), std::max( 0.0f, s.size.z ) };

	// How much of it the projectors cover.
	std::vector< StageFace > faces = Faces( st );
	std::vector< std::vector< Lighting > > lighting;
	for( const StageProjector& p : st.projectors )
		lighting.push_back( LightingOf( faces, p ) );
	Section( "Lit", "by the projectors" );
	bool any = false;
	for( int i = 0; i < (int)faces.size(); ++i )
	{
		if( faces[ i ].surface != index )
			continue;
		std::vector< std::vector< Vec3 > > lit;
		std::string by;
		for( int p = 0; p < (int)lighting.size(); ++p )
			for( const Lighting& l : lighting[ p ] )
				if( l.face == i )
				{
					lit.push_back( l.polygon );
					by += ( by.empty() ? "" : ", " ) + std::to_string( p + 1 );
				}
		float covered = 0, overlapped = 0;
		Coverage( faces[ i ], lit, covered, overlapped );
		if( lit.empty() && s.kind == StageSurface::Kind::Box )
			continue;
		any = true;
		ImGui::Text( "%s", SurfaceLabel( st, faces[ i ] ).c_str() );
		ImGui::Indent( Ui( 10 ) );
		if( lit.empty() )
			ImGui::TextDisabled( "No projector reaches it" );
		else
		{
			ImGui::Text( "%.0f %% covered, by %s", covered * 100, by.c_str() );
			if( overlapped > 0 )
				ImGui::Text( "%.0f %% where two overlap: blend there", overlapped * 100 );
		}
		ImGui::Unindent( Ui( 10 ) );
	}
	if( !any )
		ImGui::TextDisabled( "No projector reaches it" );
	ImGui::Spacing();
	if( ImGui::Button( "Duplicate" ) )
	{
		StageSurface copy = s;
		copy.id.clear();
		copy.name += " copy";
		copy.position.x += copy.size.x + 1;
		st.surfaces.insert( st.surfaces.begin() + index + 1, copy );
		setup.EnsureIds();
		stage.index = index + 1;
		return;
	}
	ImGui::SameLine();
	if( ImGui::Button( "Delete" ) )
	{
		st.surfaces.erase( st.surfaces.begin() + index );
		stage.selected = Selected::None;
		stage.index    = -1;
	}
}

// The projector's screen (a new one when it has none) gets a slice for each surface it lights: the whole composition,
// its corners where the surface's corners fall in the projector's picture.
void MakeSlicesFrom( App& app, AdvancedOutputSetup& setup, int index )
{
	Stage& st         = setup.stage;
	StageProjector& p = st.projectors[ index ];
	int screenIndex   = -1;
	for( size_t s = 0; s < setup.screens.size(); ++s )
		if( !p.screen.empty() && setup.screens[ s ].id == p.screen )
			screenIndex = (int)s;
	if( screenIndex < 0 )
	{
		Screen screen;
		screen.name   = p.name;
		screen.width  = p.width;
		screen.height = p.height;
		setup.screens.push_back( screen );
		setup.EnsureIds();
		screenIndex = (int)setup.screens.size() - 1;
		p.screen    = setup.screens.back().id;
	}
	Screen& screen = setup.screens[ screenIndex ];
	float cw = (float)app.composition.width, ch = (float)app.composition.height;
	float sx = (float)screen.width / std::max( 1, p.width ), sy = (float)screen.height / std::max( 1, p.height );
	std::vector< StageFace > faces = Faces( st );
	std::vector< Slice > slices;
	for( const Lighting& l : LightingOf( faces, p ) )
	{
		Vec2 corners[ 4 ];
		if( !FaceInPicture( p, faces[ l.face ], corners ) )
			continue;
		Slice slice = AdvancedOutputSetup::MakeSlice( SurfaceLabel( st, faces[ l.face ] ), cw, ch, (float)screen.width, (float)screen.height );
		slice.SetWarp( WarpMode::Perspective );
		for( int i = 0; i < 4; ++i )
			slice.corners[ i ] = { corners[ i ].x * sx, corners[ i ].y * sy };
		slices.push_back( slice );
	}
	// Largest first: the list's last slice is drawn over the others, so smaller surfaces (in front) end up on top.
	screen.slices = slices;
	setup.EnsureIds();
	app.SetStatus( std::to_string( slices.size() ) + " slice" + ( slices.size() == 1 ? "" : "s" ) + " made for " + screen.name + " from " + p.name
	               + ": fine tune them in 2 Map" );
}

void DrawProjectorProps( App& app, AdvancedOutputSetup& setup, int index )
{
	Stage& st         = setup.stage;
	StageProjector& p = st.projectors[ index ];
	{
		ImVec2 at = ImGui::GetCursorScreenPos();
		std::string number = std::to_string( index + 1 );
		float bw = TextBadge( ImGui::GetWindowDrawList(), ImVec2( at.x, at.y + Ui( 1 ) ), number.c_str(), ProjectorColour( setup, index ), IM_COL32( 17, 17, 17, 255 ),
		                      ImGui::GetTextLineHeight() );
		ImGui::SetCursorScreenPos( ImVec2( at.x + bw + Ui( 6 ), at.y ) );
		ImGui::PushFont( Fonts().bold );
		ImGui::TextUnformatted( p.name.c_str() );
		ImGui::PopFont();
		ImGui::SameLine();
		ImGui::TextDisabled( "projector" );
	}
	Section( "Where", "metres, degrees" );
	if( ImGui::BeginTable( "##where", 2, ImGuiTableFlags_SizingStretchProp ) )
	{
		ImGui::TableSetupColumn( "l", ImGuiTableColumnFlags_WidthFixed, Ui( 80 ) );
		ImGui::TableSetupColumn( "v", ImGuiTableColumnFlags_WidthStretch );
		Row( "Name" );
		ImGui::InputText( "##name", &p.name );
		Row( "Position" );
		ImGui::DragFloat3( "##pos", &p.position.x, 0.05f, -1000, 1000, "%.2f" );
		Row( "Aim" );
		float aim[ 3 ] = { p.pan, p.tilt, p.roll };
		if( ImGui::DragFloat3( "##aim", aim, 0.2f, -360, 360, "%.1f\xC2\xB0" ) )
		{
			p.pan  = aim[ 0 ];
			p.tilt = std::clamp( aim[ 1 ], -90.0f, 90.0f );
			p.roll = aim[ 2 ];
		}
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex( 1 );
		ImGui::TextDisabled( "pan  \xC2\xB7  tilt  \xC2\xB7  roll" );
		Row( "Point at" );
		if( ImGui::BeginCombo( "##pointAt", "A surface's centre...", ImGuiComboFlags_HeightLarge ) )
		{
			for( const StageSurface& s : st.surfaces )
				if( ImGui::Selectable( s.name.c_str() ) )
					AimAt( p, SurfaceCentre( s ) );
			if( st.surfaces.empty() )
				ImGui::TextDisabled( "No surfaces yet" );
			ImGui::EndCombo();
		}
		ImGui::EndTable();
	}
	Section( "Lens and picture", "as its data sheet says" );
	if( ImGui::BeginTable( "##lens", 2, ImGuiTableFlags_SizingStretchProp ) )
	{
		ImGui::TableSetupColumn( "l", ImGuiTableColumnFlags_WidthFixed, Ui( 80 ) );
		ImGui::TableSetupColumn( "v", ImGuiTableColumnFlags_WidthStretch );
		Row( "Resolution" );
		int res[ 2 ] = { p.width, p.height };
		if( ImGui::InputInt2( "##res", res ) )
		{
			p.width  = res[ 0 ];
			p.height = res[ 1 ];
		}
		if( !ImGui::IsItemActive() ) // in range once left
		{
			p.width  = std::clamp( p.width, 16, 16384 );
			p.height = std::clamp( p.height, 16, 16384 );
		}
		Row( "Throw ratio" );
		if( p.throwMax > p.throwMin + 1e-3f )
			ImGui::SliderFloat( "##throw", &p.throwRatio, p.throwMin, p.throwMax, "%.2f" );
		else
			ImGui::DragFloat( "##throw", &p.throwRatio, 0.01f, 0.05f, 20, "%.2f (fixed lens)" );
		p.throwRatio = std::clamp( p.throwRatio, 0.05f, 20.0f );
		Row( "Zoom range" );
		float range[ 2 ] = { p.throwMin, p.throwMax };
		if( ImGui::DragFloat2( "##range", range, 0.01f, 0.05f, 20, "%.2f" ) )
		{
			p.throwMin = std::clamp( range[ 0 ], 0.05f, 20.0f );
			p.throwMax = std::clamp( range[ 1 ], p.throwMin, 20.0f );
		}
		if( ImGui::IsItemHovered() )
			ImGui::SetTooltip( "The throw ratios of the lens's zoom, shortest and longest (the same twice: a fixed lens)" );
		Row( "Lens shift" );
		float shift[ 2 ] = { p.shiftX * 100, p.shiftY * 100 };
		if( ImGui::DragFloat2( "##shift", shift, 0.5f, -200, 200, "%.0f %%" ) )
		{
			p.shiftX = shift[ 0 ] / 100;
			p.shiftY = shift[ 1 ] / 100;
		}
		if( ImGui::IsItemHovered() )
			ImGui::SetTooltip( "Sideways and up, in picture widths and heights (+15 %%: the picture rises by 15 %% of its height)" );
		Row( "Brightness" );
		ImGui::DragFloat( "##lumens", &p.lumens, 100, 0, 200000, "%.0f lm" );
		ImGui::EndTable();
	}

	// What it lights.
	std::vector< StageFace > faces = Faces( st );
	std::vector< Lighting > lit    = LightingOf( faces, p );
	Section( "What it lights", "worked out" );
	float shares = 0;
	for( const Lighting& l : lit )
	{
		shares += l.share;
		ImGui::Text( "%s", SurfaceLabel( st, faces[ l.face ] ).c_str() );
		ImGui::Indent( Ui( 10 ) );
		ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( theme::TextSoft ), "%.0f %% of the picture, %.1f m\xC2\xB2", l.share * 100, l.area );
		ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( l.pixelsPerMetre < 40 ? theme::Warning : theme::TextSoft ), "%.0f pixels per metre", l.pixelsPerMetre );
		ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( l.lux < 60 ? theme::Warning : theme::TextSoft ), "about %.0f lux", l.lux );
		// Overlaps with the other projectors on this face.
		for( int o = 0; o < (int)st.projectors.size(); ++o )
		{
			if( o == index )
				continue;
			for( const Lighting& other : LightingOf( { faces[ l.face ] }, st.projectors[ o ] ) )
			{
				float area = PolygonArea( Overlap( faces[ l.face ], l.polygon, other.polygon ) );
				if( area > 0.01f )
					ImGui::Text( "Overlaps %d by %.1f m\xC2\xB2: blend there", o + 1, area );
			}
		}
		ImGui::Unindent( Ui( 10 ) );
	}
	if( lit.empty() )
		ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( theme::Warning ), "It lights nothing: aim it at a surface" );
	else if( shares < 0.98f )
		ImGui::TextColored( ImGui::ColorConvertU32ToFloat4( theme::Warning ), "%.0f %% of its picture lands on nothing", ( 1 - shares ) * 100 );

	Section( "Goes out as" );
	{
		std::string current = "No screen yet";
		for( size_t s = 0; s < setup.screens.size(); ++s )
			if( !p.screen.empty() && setup.screens[ s ].id == p.screen )
				current = std::to_string( s + 1 ) + "  " + setup.screens[ s ].name;
		ImGui::SetNextItemWidth( -FLT_MIN );
		if( ImGui::BeginCombo( "##screen", current.c_str(), ImGuiComboFlags_HeightLarge ) )
		{
			for( size_t s = 0; s < setup.screens.size(); ++s )
				if( ImGui::Selectable( ( std::to_string( s + 1 ) + "  " + setup.screens[ s ].name + "##" + std::to_string( s ) ).c_str(), setup.screens[ s ].id == p.screen ) )
					p.screen = setup.screens[ s ].id;
			ImGui::Separator();
			if( ImGui::Selectable( "A new screen for it" ) )
			{
				Screen screen;
				screen.name   = p.name;
				screen.width  = p.width;
				screen.height = p.height;
				screen.slices.push_back( AdvancedOutputSetup::MakeSlice( "Slice 1", (float)app.composition.width, (float)app.composition.height, (float)p.width, (float)p.height ) );
				setup.screens.push_back( screen );
				setup.EnsureIds();
				p.screen = setup.screens.back().id;
			}
			if( ImGui::Selectable( "None" ) )
				p.screen.clear();
			ImGui::EndCombo();
		}
		if( ImGui::IsItemHovered() )
			ImGui::SetTooltip( "The screen of step 1 this projector shows: its picture is what the projector sends out" );
	}
	// Its screen's slices, one per surface it lights, already in perspective from where it stands.
	{
		int screenIndex = -1;
		for( size_t s = 0; s < setup.screens.size(); ++s )
			if( !p.screen.empty() && setup.screens[ s ].id == p.screen )
				screenIndex = (int)s;
		ImGui::BeginDisabled( lit.empty() );
		if( ImGui::Button( "Make the slices from here", ImVec2( -FLT_MIN, 0 ) ) )
		{
			if( screenIndex >= 0 && !setup.screens[ screenIndex ].slices.empty() )
				ImGui::OpenPopup( "##replaceSlices" );
			else
				MakeSlicesFrom( app, setup, index );
		}
		ImGui::EndDisabled();
		if( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) )
			ImGui::SetTooltip( lit.empty() ? "It lights nothing yet" : "A slice of its screen for each surface it lights, already in perspective from where it stands: on site "
			                                                           "only the fine tuning is left (2 Map)" );
		if( ImGui::BeginPopup( "##replaceSlices" ) )
		{
			ImGui::Text( "Replace the %d slices of %s with one per surface it lights?", (int)setup.screens[ screenIndex ].slices.size(),
			             setup.screens[ screenIndex ].name.c_str() );
			if( ImGui::Button( "Replace" ) )
			{
				MakeSlicesFrom( app, setup, index );
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if( ImGui::Button( "Cancel" ) )
				ImGui::CloseCurrentPopup();
			ImGui::EndPopup();
		}
	}
	ImGui::Spacing();
	if( ImGui::Button( "Look through it" ) )
	{
		stage.view    = ViewKind::Through;
		stage.through = index;
	}
	ImGui::SameLine();
	if( ImGui::Button( "Duplicate" ) )
	{
		StageProjector copy = p;
		copy.id.clear();
		copy.screen.clear();
		copy.name += " copy";
		copy.position.x += 3;
		st.projectors.insert( st.projectors.begin() + index + 1, copy );
		setup.EnsureIds();
		stage.index = index + 1;
		return;
	}
	ImGui::SameLine();
	if( ImGui::Button( "Delete" ) )
	{
		st.projectors.erase( st.projectors.begin() + index );
		stage.selected = Selected::None;
		stage.index    = -1;
		if( stage.through >= (int)st.projectors.size() )
			stage.through = 0;
	}
}

void DrawViewBar( const Stage& st )
{
	auto choice = []( const char* label, bool on ) {
		if( on )
		{
			ImGui::PushStyleColor( ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4( theme::Accent ) );
			ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4( theme::Accent ) );
			ImGui::PushStyleColor( ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4( theme::OnAccent ) );
		}
		bool clicked = ImGui::Button( label );
		if( on )
			ImGui::PopStyleColor( 3 );
		ImGui::SameLine( 0, Ui( 2 ) );
		return clicked;
	};
	if( choice( "3D", stage.view == ViewKind::Orbit ) )
		stage.view = ViewKind::Orbit;
	if( choice( "Top", stage.view == ViewKind::Top ) )
		stage.view = ViewKind::Top;
	if( choice( "Front", stage.view == ViewKind::Front ) )
		stage.view = ViewKind::Front;
	for( int p = 0; p < (int)st.projectors.size() && p < 8; ++p )
	{
		std::string label = "Through " + std::to_string( p + 1 ) + "##through";
		ImGui::PushID( p );
		if( choice( label.c_str(), stage.view == ViewKind::Through && stage.through == p ) )
		{
			stage.view    = ViewKind::Through;
			stage.through = p;
		}
		ImGui::PopID();
	}
	ImGui::SameLine( 0, Ui( 14 ) );
	ImGui::AlignTextToFramePadding();
	ImGui::TextDisabled( "Colour" );
	ImGui::SameLine();
	const char* names[] = { "Projectors", "Brightness (lux)", "Sharpness (px/m)" };
	ImGui::SetNextItemWidth( Ui( 150 ) );
	if( ImGui::BeginCombo( "##colouring", names[ (int)stage.colouring ] ) )
	{
		for( int i = 0; i < 3; ++i )
			if( ImGui::Selectable( names[ i ], (int)stage.colouring == i ) )
				stage.colouring = (Colouring)i;
		ImGui::EndCombo();
	}
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "Brightness: red under about 100 lux, green from 500. Sharpness: red under about 40 pixels per metre, green from 150." );
}
}

void DrawStageStep( App& app, AdvancedOutputSetup& setup, float height )
{
	Stage& st = setup.stage;
	if( st.Empty() )
	{
		// Where a stage starts: an example to change, or nothing.
		ImGui::BeginChild( "##stageEmpty", ImVec2( 0, height ) );
		ImGui::Dummy( ImVec2( 1, Ui( 30 ) ) );
		ImGui::PushFont( Fonts().bold );
		ImGui::TextUnformatted( "The space, and the projectors in it" );
		ImGui::PopFont();
		ImGui::PushTextWrapPos( Ui( 640 ) );
		ImGui::TextWrapped( "Put in the surfaces (a façade, walls, blocks) at their real size in metres, then the projectors where they would "
		                    "really stand, with their lens and brightness. You see what each one lights, how sharp and bright it is there, "
		                    "and where two overlap, before you get to the place." );
		ImGui::PopTextWrapPos();
		ImGui::Spacing();
		if( ImGui::Button( "Start with a façade 20 x 12 m" ) )
		{
			st = Stage::MakeDefault();
			setup.EnsureIds();
		}
		ImGui::SameLine();
		if( ImGui::Button( "Start empty" ) )
		{
			StageSurface s;
			s.name     = "Surface 1";
			s.position = { 0, 2.5f, 0 };
			s.size     = { 8, 5, 0 };
			st.surfaces.push_back( s );
			setup.EnsureIds();
		}
		ImGui::EndChild();
		return;
	}
	if( stage.selected == Selected::Surface && ( stage.index < 0 || stage.index >= (int)st.surfaces.size() ) )
		stage.selected = Selected::None;
	if( stage.selected == Selected::Projector && ( stage.index < 0 || stage.index >= (int)st.projectors.size() ) )
		stage.selected = Selected::None;

	if( !ImGui::BeginTable( "##stage", 3, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV, ImVec2( 0, height ) ) )
		return;
	ImGui::TableSetupColumn( "list", ImGuiTableColumnFlags_WidthFixed, Ui( 250 ) );
	ImGui::TableSetupColumn( "view", ImGuiTableColumnFlags_WidthStretch );
	ImGui::TableSetupColumn( "props", ImGuiTableColumnFlags_WidthFixed, Ui( 320 ) );
	ImGui::TableNextRow();

	ImGui::TableSetColumnIndex( 0 );
	ImGui::BeginChild( "##stageList", ImVec2( 0, height ) );
	DrawLists( app, setup );
	ImGui::EndChild();

	ImGui::TableSetColumnIndex( 1 );
	ImGui::BeginChild( "##stageMiddle", ImVec2( 0, height ) );
	DrawViewBar( st );
	ImGui::SameLine( 0, Ui( 14 ) );
	if( ImGui::Button( stage.detached ? "Back in here" : "Detach the view" ) )
		stage.detached = !stage.detached;
	if( ImGui::IsItemHovered() )
		ImGui::SetTooltip( "The 3D view in a window of its own, to put large on another display" );
	ImVec2 avail = ImGui::GetContentRegionAvail();
	if( stage.detached )
	{
		ImGui::Dummy( ImVec2( 1, Ui( 20 ) ) );
		ImGui::TextDisabled( "The view is in its own window." );
	}
	else
		DrawView( app, setup, ImVec2( std::max( Ui( 100 ), avail.x ), std::max( Ui( 100 ), avail.y ) ) );
	ImGui::EndChild();

	ImGui::TableSetColumnIndex( 2 );
	ImGui::BeginChild( "##stageProps", ImVec2( 0, height ) );
	if( stage.selected == Selected::Surface )
		DrawSurfaceProps( setup, stage.index );
	else if( stage.selected == Selected::Projector )
		DrawProjectorProps( app, setup, stage.index );
	else
	{
		ImGui::PushTextWrapPos( 0 );
		ImGui::TextDisabled( "Click a surface or a projector, in the list or in the view, for its settings." );
		ImGui::PopTextWrapPos();
	}
	ImGui::EndChild();
	ImGui::EndTable();
}

bool DrawStageWindow( App& app, AdvancedOutputSetup& setup )
{
	if( !stage.detached )
		return false;
	ImGui::SetNextWindowSize( ImVec2( Ui( 900 ), Ui( 600 ) ), ImGuiCond_FirstUseEver );
	bool open = true;
	if( ImGui::Begin( "Stage view##stageWindow", &open ) )
	{
		DrawViewBar( setup.stage );
		ImGui::NewLine();
		ImVec2 avail = ImGui::GetContentRegionAvail();
		if( !setup.stage.Empty() )
			DrawView( app, setup, ImVec2( std::max( Ui( 100 ), avail.x ), std::max( Ui( 100 ), avail.y ) ) );
	}
	ImGui::End();
	stage.detached = open;
	return open;
}
}
