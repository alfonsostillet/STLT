#include "BuiltinEffects.h"
#include <cstdio>
#include <map>
#include "gl/Program.h"
#include "render/Fbo.h"

namespace stltvj
{
namespace
{
constexpr int kMaxParams = 12;

const char* kHeader = R"(
uniform sampler2D uInput;
uniform sampler2D uPrevious;
uniform vec2 uResolution;
uniform float uTime;
uniform float uBeat;
uniform float p0, p1, p2, p3, p4, p5, p6, p7, p8, p9, p10, p11;
in vec2 vUv;
out vec4 fragColor;
const float PI = 3.14159265;

vec4 src( vec2 uv ) { return texture( uInput, uv ); }
vec3 unpremultiply( vec4 c ) { return c.a > 0.0 ? c.rgb / c.a : vec3( 0.0 ); }
float luma( vec3 c ) { return dot( c, vec3( 0.2126, 0.7152, 0.0722 ) ); }
vec3 rgb2hsv( vec3 c )
{
	vec4 K = vec4( 0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0 );
	vec4 p = mix( vec4( c.bg, K.wz ), vec4( c.gb, K.xy ), step( c.b, c.g ) );
	vec4 q = mix( vec4( p.xyw, c.r ), vec4( c.r, p.yzx ), step( p.x, c.r ) );
	float d = q.x - min( q.w, q.y );
	return vec3( abs( q.z + ( q.w - q.y ) / ( 6.0 * d + 1e-10 ) ), d / ( q.x + 1e-10 ), q.x );
}
vec3 hsv2rgb( vec3 c )
{
	vec3 p = abs( fract( c.xxx + vec3( 1.0, 2.0 / 3.0, 1.0 / 3.0 ) ) * 6.0 - 3.0 );
	return c.z * mix( vec3( 1.0 ), clamp( p - 1.0, 0.0, 1.0 ), c.y );
}
vec2 mirrorRepeat( vec2 uv ) { return 1.0 - abs( 1.0 - mod( uv, 2.0 ) ); }
)";

const char* kFooter = R"(
void main() { fragColor = effect( vUv ); }
)";

#define COLOR_PARAMS( prefix, r, g, b )                                              \
	BuiltinParam{ prefix "Red", ParamKind::Red, 0, 1, r, {} },                      \
		BuiltinParam{ prefix "Green", ParamKind::Green, 0, 1, g, {} },              \
		BuiltinParam{ prefix "Blue", ParamKind::Blue, 0, 1, b, {} }

std::vector< BuiltinDef > MakeDefs()
{
	std::vector< BuiltinDef > d;
	d.push_back( { "Brightness & Contrast", false, false,
	               { { "Brightness", ParamKind::Float, -1, 1, 0, {} }, { "Contrast", ParamKind::Float, 0, 3, 1, {} }, { "Gamma", ParamKind::Float, 0.2f, 3, 1, {} } },
	               R"(
vec4 effect( vec2 uv )
{
	vec4 c = src( uv );
	vec3 rgb = unpremultiply( c );
	rgb = pow( max( rgb, 0.0 ), vec3( 1.0 / p2 ) );
	rgb = ( rgb - 0.5 ) * p1 + 0.5 + p0;
	return vec4( clamp( rgb, 0.0, 1.0 ) * c.a, c.a );
}
)" } );
	d.push_back( { "Hue & Saturation", false, false,
	               { { "Hue", ParamKind::Float, -180, 180, 0, {} }, { "Saturation", ParamKind::Float, 0, 3, 1, {} }, { "Lightness", ParamKind::Float, -1, 1, 0, {} } },
	               R"(
vec4 effect( vec2 uv )
{
	vec4 c = src( uv );
	vec3 hsv = rgb2hsv( unpremultiply( c ) );
	hsv.x = fract( hsv.x + p0 / 360.0 );
	hsv.y = clamp( hsv.y * p1, 0.0, 1.0 );
	vec3 rgb = hsv2rgb( hsv );
	rgb = p2 >= 0.0 ? mix( rgb, vec3( 1.0 ), p2 ) : rgb * ( 1.0 + p2 );
	return vec4( rgb * c.a, c.a );
}
)" } );
	d.push_back( { "Invert", false, false, { { "Amount", ParamKind::Float, 0, 1, 1, {} } }, R"(
vec4 effect( vec2 uv )
{
	vec4 c = src( uv );
	vec3 rgb = unpremultiply( c );
	return vec4( mix( rgb, 1.0 - rgb, p0 ) * c.a, c.a );
}
)" } );
	d.push_back( { "Colorize", false, false, { COLOR_PARAMS( "Color ", 1.0f, 0.35f, 0.6f ), { "Amount", ParamKind::Float, 0, 1, 1, {} } }, R"(
vec4 effect( vec2 uv )
{
	vec4 c = src( uv );
	vec3 rgb = unpremultiply( c );
	vec3 tinted = luma( rgb ) * vec3( p0, p1, p2 ) * 1.5;
	return vec4( clamp( mix( rgb, tinted, p3 ), 0.0, 1.0 ) * c.a, c.a );
}
)" } );
	d.push_back( { "Threshold", false, false, { { "Level", ParamKind::Float, 0, 1, 0.5f, {} }, { "Softness", ParamKind::Float, 0, 0.5f, 0.02f, {} } }, R"(
vec4 effect( vec2 uv )
{
	vec4 c = src( uv );
	float l = luma( unpremultiply( c ) );
	float v = smoothstep( p0 - p1, p0 + p1 + 1e-4, l );
	return vec4( vec3( v ) * c.a, c.a );
}
)" } );
	d.push_back( { "Mirror", false, false,
	               { { "Mode", ParamKind::Option, 0, 4, 0, { "Left to Right", "Right to Left", "Bottom to Top", "Top to Bottom", "Quad" } } }, R"(
vec4 effect( vec2 uv )
{
	int mode = int( p0 + 0.5 );
	if( ( mode == 0 || mode == 4 ) && uv.x > 0.5 ) uv.x = 1.0 - uv.x;
	if( mode == 1 && uv.x < 0.5 ) uv.x = 1.0 - uv.x;
	if( ( mode == 2 || mode == 4 ) && uv.y > 0.5 ) uv.y = 1.0 - uv.y;
	if( mode == 3 && uv.y < 0.5 ) uv.y = 1.0 - uv.y;
	return src( uv );
}
)" } );
	d.push_back( { "Kaleidoscope", false, false,
	               { { "Segments", ParamKind::Integer, 2, 24, 6, {} }, { "Rotation", ParamKind::Float, 0, 360, 0, {} }, { "Zoom", ParamKind::Float, 0.25f, 4, 1, {} } },
	               R"(
vec4 effect( vec2 uv )
{
	float aspect = uResolution.x / uResolution.y;
	vec2 p = ( uv - 0.5 ) * vec2( aspect, 1.0 ) / p2;
	float segment = 2.0 * PI / max( p0, 2.0 );
	float a = mod( atan( p.y, p.x ) + radians( p1 ), segment );
	a = abs( a - segment * 0.5 );
	p = length( p ) * vec2( cos( a ), sin( a ) );
	return src( mirrorRepeat( p / vec2( aspect, 1.0 ) + 0.5 ) );
}
)" } );
	d.push_back( { "Tile", false, false,
	               { { "Columns", ParamKind::Integer, 1, 16, 2, {} }, { "Rows", ParamKind::Integer, 1, 16, 2, {} }, { "Mirror", ParamKind::Bool, 0, 1, 0, {} } }, R"(
vec4 effect( vec2 uv )
{
	vec2 t = uv * vec2( p0, p1 );
	return src( p2 > 0.5 ? mirrorRepeat( t ) : fract( t ) );
}
)" } );
	d.push_back( { "Pixelate", false, false, { { "Size", ParamKind::Float, 1, 256, 16, {} } }, R"(
vec4 effect( vec2 uv )
{
	vec2 cell = vec2( max( p0, 1.0 ) ) / uResolution;
	return src( ( floor( uv / cell ) + 0.5 ) * cell );
}
)" } );
	d.push_back( { "RGB Split", false, false, { { "Distance", ParamKind::Float, 0, 200, 12, {} }, { "Angle", ParamKind::Float, 0, 360, 0, {} } }, R"(
vec4 effect( vec2 uv )
{
	vec2 offset = vec2( cos( radians( p1 ) ), sin( radians( p1 ) ) ) * p0 / uResolution;
	vec4 r = src( uv + offset ), g = src( uv ), b = src( uv - offset );
	return vec4( r.r, g.g, b.b, max( max( r.a, g.a ), b.a ) );
}
)" } );
	d.push_back( { "Blur", false, false, { { "Radius", ParamKind::Float, 0, 64, 8, {} } }, R"(
vec4 effect( vec2 uv )
{
	if( p0 < 0.5 ) return src( uv );
	// Golden-angle spiral: even coverage of the disc with few taps.
	vec4 sum = vec4( 0.0 );
	float total = 0.0;
	for( int i = 0; i < 48; ++i )
	{
		float r = sqrt( ( float( i ) + 0.5 ) / 48.0 );
		float a = float( i ) * 2.39996323;
		float w = 1.0 - r * 0.7;
		sum += src( uv + vec2( cos( a ), sin( a ) ) * r * p0 / uResolution ) * w;
		total += w;
	}
	return sum / total;
}
)" } );
	d.push_back( { "Edge Detect", false, false, { { "Strength", ParamKind::Float, 0, 5, 1.5f, {} }, { "Keep Colour", ParamKind::Bool, 0, 1, 0, {} } }, R"(
vec4 effect( vec2 uv )
{
	vec2 t = 1.0 / uResolution;
	float tl = luma( src( uv + t * vec2( -1, 1 ) ).rgb ), tc = luma( src( uv + t * vec2( 0, 1 ) ).rgb ), tr = luma( src( uv + t * vec2( 1, 1 ) ).rgb );
	float ml = luma( src( uv + t * vec2( -1, 0 ) ).rgb ), mr = luma( src( uv + t * vec2( 1, 0 ) ).rgb );
	float bl = luma( src( uv + t * vec2( -1, -1 ) ).rgb ), bc = luma( src( uv + t * vec2( 0, -1 ) ).rgb ), br = luma( src( uv + t * vec2( 1, -1 ) ).rgb );
	float gx = tr + 2.0 * mr + br - tl - 2.0 * ml - bl;
	float gy = tl + 2.0 * tc + tr - bl - 2.0 * bc - br;
	float e = clamp( length( vec2( gx, gy ) ) * p0, 0.0, 1.0 );
	vec4 c = src( uv );
	return p1 > 0.5 ? vec4( c.rgb * e, c.a * e ) : vec4( vec3( e ), 1.0 ) * c.a;
}
)" } );
	d.push_back( { "Strobe", false, false,
	               { { "Rate", ParamKind::Option, 0, 5, 2, { "1/8 beat", "1/4 beat", "1/2 beat", "1 beat", "2 beats", "4 beats" } },
	                 { "Duty", ParamKind::Float, 0.05f, 1, 0.5f, {} },
	                 { "Mode", ParamKind::Option, 0, 2, 0, { "Flash Color", "Blackout", "Transparent" } },
	                 COLOR_PARAMS( "Flash ", 1, 1, 1 ) },
	               R"(
vec4 effect( vec2 uv )
{
	float beats = 0.125 * pow( 2.0, floor( p0 + 0.5 ) );
	bool on = fract( uBeat / beats ) < p1;
	vec4 c = src( uv );
	int mode = int( p2 + 0.5 );
	if( !on ) return mode == 0 ? c : ( mode == 1 ? vec4( 0.0, 0.0, 0.0, c.a ) : vec4( 0.0 ) );
	return mode == 0 ? vec4( p3, p4, p5, 1.0 ) : c;
}
)" } );
	d.push_back( { "Trails", false, true,
	               { { "Length", ParamKind::Float, 0, 0.98f, 0.85f, {} }, { "Mode", ParamKind::Option, 0, 1, 0, { "Lighten", "Over" } } }, R"(
vec4 effect( vec2 uv )
{
	vec4 c = src( uv );
	vec4 previous = texture( uPrevious, uv ) * p0;
	return int( p1 + 0.5 ) == 0 ? max( c, previous ) : c + previous * ( 1.0 - c.a );
}
)" } );

	// Sources.
	d.push_back( { "Solid Color", true, false, { COLOR_PARAMS( "Color ", 1, 1, 1 ), { "Opacity", ParamKind::Alpha, 0, 1, 1, {} } }, R"(
vec4 effect( vec2 uv ) { return vec4( p0, p1, p2, 1.0 ) * p3; }
)" } );
	d.push_back( { "Gradient", true, false,
	               { COLOR_PARAMS( "Start ", 0.05f, 0.1f, 0.45f ), COLOR_PARAMS( "End ", 1.0f, 0.3f, 0.5f ), { "Angle", ParamKind::Float, 0, 360, 90, {} },
	                 { "Scroll Beats", ParamKind::Option, 0, 5, 0, { "Off", "1", "2", "4", "8", "16" } } },
	               R"(
vec4 effect( vec2 uv )
{
	vec2 dir = vec2( cos( radians( p6 ) ), sin( radians( p6 ) ) );
	float t = dot( uv - 0.5, dir ) + 0.5;
	int scroll = int( p7 + 0.5 );
	if( scroll > 0 ) t = 1.0 - abs( 1.0 - 2.0 * fract( t * 0.5 + uBeat / pow( 2.0, float( scroll - 1 ) ) * 0.5 ) );
	return vec4( mix( vec3( p0, p1, p2 ), vec3( p3, p4, p5 ), clamp( t, 0.0, 1.0 ) ), 1.0 );
}
)" } );
	d.push_back( { "Lines", true, false,
	               { { "Count", ParamKind::Integer, 1, 64, 8, {} }, { "Width", ParamKind::Float, 0, 1, 0.5f, {} }, { "Angle", ParamKind::Float, 0, 360, 0, {} },
	                 { "Scroll Beats", ParamKind::Option, 0, 5, 3, { "Off", "1", "2", "4", "8", "16" } }, COLOR_PARAMS( "Color ", 1, 1, 1 ) },
	               R"(
vec4 effect( vec2 uv )
{
	float aspect = uResolution.x / uResolution.y;
	vec2 dir = vec2( cos( radians( p2 ) ), sin( radians( p2 ) ) );
	float t = dot( ( uv - 0.5 ) * vec2( aspect, 1.0 ), dir ) * p0 / max( abs( dir.x ) * aspect + abs( dir.y ), 1e-3 );
	int scroll = int( p3 + 0.5 );
	if( scroll > 0 ) t += uBeat / pow( 2.0, float( scroll - 1 ) );
	float f = fract( t );
	float edge = fwidth( t ) * 1.5;
	float on = smoothstep( 0.0, edge, f ) * ( 1.0 - smoothstep( p1 - edge, p1, f ) );
	return vec4( p4, p5, p6, 1.0 ) * on;
}
)" } );
	d.push_back( { "Checker", true, false, { { "Size", ParamKind::Float, 2, 512, 64, {} }, COLOR_PARAMS( "Color ", 1, 1, 1 ), { "Flip On Beat", ParamKind::Bool, 0, 1, 0, {} } }, R"(
vec4 effect( vec2 uv )
{
	vec2 cell = floor( uv * uResolution / max( p0, 1.0 ) );
	float on = mod( cell.x + cell.y + ( p4 > 0.5 ? floor( uBeat ) : 0.0 ), 2.0 );
	return vec4( p1, p2, p3, 1.0 ) * on;
}
)" } );
	return d;
}

struct ProgramEntry
{
	Program program;
	bool failed = false;
	GLint params[ kMaxParams ] = {};
	GLint transform = -1, texRect = -1, resolution = -1, time = -1, beat = -1, input = -1, previous = -1;
};

std::map< const BuiltinDef*, std::unique_ptr< ProgramEntry > >& Programs()
{
	static std::map< const BuiltinDef*, std::unique_ptr< ProgramEntry > > programs;
	return programs;
}

GLuint& SharedVao()
{
	static GLuint vao = 0;
	return vao;
}

ProgramEntry* ProgramFor( const BuiltinDef& def, std::string& error )
{
	std::unique_ptr< ProgramEntry >& entry = Programs()[ &def ];
	if( !entry )
	{
		entry                = std::make_unique< ProgramEntry >();
		std::string fragment = std::string( kHeader ) + def.glsl + kFooter;
		if( !entry->program.Build( kQuadVertexShader, fragment.c_str() ) )
		{
			entry->failed = true;
			std::fprintf( stderr, "%s: %s\n", def.name, entry->program.Error().c_str() );
		}
		GLuint id = entry->program.Id();
		for( int i = 0; i < kMaxParams; ++i )
			entry->params[ i ] = glGetUniformLocation( id, ( "p" + std::to_string( i ) ).c_str() );
		entry->transform  = glGetUniformLocation( id, "uTransform" );
		entry->texRect    = glGetUniformLocation( id, "uTexRect" );
		entry->resolution = glGetUniformLocation( id, "uResolution" );
		entry->time       = glGetUniformLocation( id, "uTime" );
		entry->beat       = glGetUniformLocation( id, "uBeat" );
		entry->input      = glGetUniformLocation( id, "uInput" );
		entry->previous   = glGetUniformLocation( id, "uPrevious" );
	}
	if( entry->failed )
	{
		error = entry->program.Error();
		return nullptr;
	}
	return entry.get();
}

class ShaderEffect : public Effect
{
public:
	explicit ShaderEffect( const BuiltinDef& definition ) : def( definition )
	{
		for( const BuiltinParam& b : def.params )
		{
			ParamInfo p;
			p.name = p.displayName = b.name;
			p.kind                 = b.kind;
			p.min                  = b.min;
			p.max                  = b.max;
			p.defaultValue         = b.defaultValue;
			for( size_t i = 0; i < b.options.size(); ++i )
			{
				p.options.push_back( b.options[ i ] );
				p.optionValues.push_back( (float)i );
			}
			params.push_back( p );
		}
		InitValues();
	}

	std::string Name() const override { return def.name; }
	std::string Id() const override { return std::string( "builtin:" ) + def.name; }
	bool IsSource() const override { return def.source; }
	std::string Error() const override { return error; }

	bool Process( GLuint input, int width, int height, GLuint targetFbo, const FrameInfo& frame ) override
	{
		ProgramEntry* entry = ProgramFor( def, error );
		if( !entry )
			return false;
		if( def.feedback )
			history.Ensure( width, height );
		if( !SharedVao() )
			glGenVertexArrays( 1, &SharedVao() );

		glBindVertexArray( SharedVao() );
		entry->program.Use();
		const float identity[ 9 ] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
		glUniformMatrix3fv( entry->transform, 1, GL_FALSE, identity );
		glUniform4f( entry->texRect, 0, 1, 1, 0 ); // FBO textures: v grows upwards
		glUniform2f( entry->resolution, (float)width, (float)height );
		glUniform1f( entry->time, (float)frame.time );
		glUniform1f( entry->beat, (float)frame.beat );
		glUniform1i( entry->input, 0 );
		glUniform1i( entry->previous, 1 );
		for( int i = 0; i < (int)params.size() && i < kMaxParams; ++i )
			glUniform1f( entry->params[ i ], effective[ i ] );
		glActiveTexture( GL_TEXTURE1 );
		glBindTexture( GL_TEXTURE_2D, def.feedback ? history.Texture() : 0 );
		glActiveTexture( GL_TEXTURE0 );
		glBindTexture( GL_TEXTURE_2D, input );
		glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );

		if( def.feedback )
		{
			glBindFramebuffer( GL_READ_FRAMEBUFFER, targetFbo );
			glBindFramebuffer( GL_DRAW_FRAMEBUFFER, history.Framebuffer() );
			glBlitFramebuffer( 0, 0, width, height, 0, 0, width, height, GL_COLOR_BUFFER_BIT, GL_NEAREST );
			glBindFramebuffer( GL_FRAMEBUFFER, targetFbo );
		}
		ResetGlState();
		return true;
	}

private:
	const BuiltinDef& def;
	Fbo history;
	std::string error;
};
}

const std::vector< BuiltinDef >& BuiltinDefs()
{
	static const std::vector< BuiltinDef > defs = MakeDefs();
	return defs;
}

std::unique_ptr< Effect > CreateBuiltin( const std::string& name )
{
	for( const BuiltinDef& def : BuiltinDefs() )
		if( name == def.name )
			return std::make_unique< ShaderEffect >( def );
	return nullptr;
}

void ReleaseBuiltinPrograms()
{
	Programs().clear();
	if( SharedVao() )
		glDeleteVertexArrays( 1, &SharedVao() );
	SharedVao() = 0;
}
}
