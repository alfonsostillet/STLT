#include "OutputRenderer.h"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cmath>
#include "fx/Effect.h"
#include "render/Compositor.h"

namespace stltvj
{
namespace
{
const char* kSliceVertex = R"(
layout( location = 0 ) in vec2 aPosition; // clip space
layout( location = 1 ) in vec2 aSliceUv;  // 0..1 across the slice, v down
layout( location = 2 ) in vec2 aSource;   // composition texture coordinates
out vec2 vSliceUv;
out vec2 vSource;
void main()
{
	vSliceUv = aSliceUv;
	vSource = aSource;
	gl_Position = vec4( aPosition, 0.0, 1.0 );
}
)";

const char* kSliceFragment = R"(
uniform sampler2D uComposition;
uniform float uOpacity;
uniform vec4 uEdges;  // left, right, top, bottom widths
uniform vec3 uRamp;   // power, luminance, gamma
uniform int uTestPattern;
uniform vec3 uColour; // the slice's own colour in the test pattern
in vec2 vSliceUv;
in vec2 vSource;
out vec4 fragColor;

float Ramp( float distance, float width )
{
	if( width <= 0.0 )
		return 1.0;
	float x = clamp( distance / width, 0.0, 1.0 );
	float f = x < 0.5 ? uRamp.y * pow( 2.0 * x, uRamp.x ) : 1.0 - ( 1.0 - uRamp.y ) * pow( 2.0 * ( 1.0 - x ), uRamp.x );
	return pow( max( f, 0.0 ), 1.0 / max( uRamp.z, 0.1 ) );
}

void main()
{
	vec4 c = texture( uComposition, vSource );
	if( uTestPattern != 0 )
	{
		// A grid of tenths and a frame: straight lines show warps, the colour tells slices apart.
		vec2 g = abs( fract( vSliceUv * 10.0 + 0.5 ) - 0.5 ) / fwidth( vSliceUv * 10.0 );
		float line = 1.0 - clamp( min( g.x, g.y ) - 0.5, 0.0, 1.0 );
		vec2 e = min( vSliceUv, 1.0 - vSliceUv ) / fwidth( vSliceUv );
		float frame = 1.0 - clamp( min( e.x, e.y ) - 2.0, 0.0, 1.0 );
		vec2 cross = abs( vSliceUv - 0.5 ) / fwidth( vSliceUv );
		float centre = 1.0 - clamp( min( cross.x, cross.y ) - 1.0, 0.0, 1.0 );
		c = vec4( mix( uColour * 0.25, vec3( 1.0 ), max( max( line * 0.6, frame ), centre ) ), 1.0 );
	}
	float edge = Ramp( vSliceUv.x, uEdges.x ) * Ramp( 1.0 - vSliceUv.x, uEdges.y ) * Ramp( vSliceUv.y, uEdges.z ) * Ramp( 1.0 - vSliceUv.y, uEdges.w );
	fragColor = c * ( uOpacity * edge );
}
)";

// Screen colour: brightness, contrast, RGB offsets and opacity, on opaque output.
const char* kFinishFragment = R"(
uniform sampler2D uTex0;
uniform float uOpacity;
uniform float uBrightness;
uniform float uContrast;
uniform vec3 uRgb;
in vec2 vUv;
out vec4 fragColor;
void main()
{
	vec3 c = texture( uTex0, vUv ).rgb;
	c = ( c - 0.5 ) * ( 1.0 + uContrast ) + 0.5 + uBrightness;
	c += uRgb;
	fragColor = vec4( clamp( c, 0.0, 1.0 ) * uOpacity, 1.0 );
}
)";

// A distinct colour per slice for the test pattern.
void SliceColour( int index, float out[ 3 ] )
{
	float h = std::fmod( index * 0.61803398875f, 1.0f ) * 6.0f;
	float x = 1 - std::fabs( std::fmod( h, 2.0f ) - 1 );
	float rgb[ 6 ][ 3 ] = { { 1, x, 0 }, { x, 1, 0 }, { 0, 1, x }, { 0, x, 1 }, { x, 0, 1 }, { 1, 0, x } };
	int k = std::clamp( (int)h, 0, 5 );
	for( int i = 0; i < 3; ++i )
		out[ i ] = rgb[ k ][ i ];
}
}

bool OutputRenderer::Init( std::string& error )
{
	if( !sliceProgram.Build( kSliceVertex, kSliceFragment ) )
	{
		error = "output slice shader " + sliceProgram.Error();
		return false;
	}
	if( !finishProgram.Build( kQuadVertexShader, kFinishFragment ) )
	{
		error = "output finish shader " + finishProgram.Error();
		return false;
	}
	glGenVertexArrays( 1, &vao );
	glGenBuffers( 1, &vbo );
	glBindVertexArray( vao );
	glBindBuffer( GL_ARRAY_BUFFER, vbo );
	for( int a = 0; a < 3; ++a )
	{
		glEnableVertexAttribArray( a );
		glVertexAttribPointer( a, 2, GL_FLOAT, GL_FALSE, 6 * sizeof( float ), (const void*)( a * 2 * sizeof( float ) ) );
	}
	glBindVertexArray( 0 );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	return true;
}

void OutputRenderer::Shutdown()
{
	CloseWindows();
	screens.clear();
	if( vbo )
		glDeleteBuffers( 1, &vbo );
	if( vao )
		glDeleteVertexArrays( 1, &vao );
	vbo = vao = 0;
}

void OutputRenderer::DrawSlice( const Slice& slice, int index, const Screen& screen, int compWidth, int compHeight )
{
	float W = (float)screen.width, H = (float)screen.height;
	float cw = (float)std::max( compWidth, 1 ), ch = (float)std::max( compHeight, 1 );
	vertices.clear();
	auto push = [ & ]( Vec2 out, float u, float v, Vec2 src ) {
		vertices.insert( vertices.end(), { out.x / W * 2 - 1, 1 - out.y / H * 2, u, v, src.x / cw, 1 - src.y / ch } );
	};

	if( slice.polygon )
	{
		if( slice.input.size() < 3 || slice.output.size() != slice.input.size() )
			return;
		std::vector< int > triangles = Triangulate( slice.input );
		float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
		for( Vec2 p : slice.input )
		{
			minX = std::min( minX, p.x );
			minY = std::min( minY, p.y );
			maxX = std::max( maxX, p.x );
			maxY = std::max( maxY, p.y );
		}
		float bw = std::max( maxX - minX, 1e-3f ), bh = std::max( maxY - minY, 1e-3f );
		for( int i : triangles )
			push( slice.output[ i ], ( slice.input[ i ].x - minX ) / bw, ( slice.input[ i ].y - minY ) / bh, slice.input[ i ] );
	}
	else
	{
		if( slice.input.size() != 4 )
			return;
		// Enough cells that perspective and curves stay smooth on a big screen.
		int n = slice.warp == WarpMode::Perspective ? 24
		        : slice.warp == WarpMode::Contour   ? std::clamp( 8 * (int)slice.contour.size(), 32, 96 )
		                                            : std::clamp( 8 * ( std::max( slice.gridColumns, slice.gridRows ) - 1 ), 16, 96 );
		std::vector< Vec2 > positions = slice.MapGrid( n ), sources( positions.size() );
		for( int r = 0; r <= n; ++r )
			for( int c = 0; c <= n; ++c )
				sources[ (size_t)r * ( n + 1 ) + c ] = slice.Source( (float)c / n, (float)r / n );
		auto corner = [ & ]( int c, int r ) {
			size_t i = (size_t)r * ( n + 1 ) + c;
			push( positions[ i ], (float)c / n, (float)r / n, sources[ i ] );
		};
		for( int r = 0; r < n; ++r )
			for( int c = 0; c < n; ++c )
			{
				corner( c, r );
				corner( c + 1, r );
				corner( c + 1, r + 1 );
				corner( c, r );
				corner( c + 1, r + 1 );
				corner( c, r + 1 );
			}
	}
	if( vertices.empty() )
		return;

	float colour[ 3 ];
	SliceColour( index, colour );
	sliceProgram.Use();
	glUniform1i( sliceProgram.Uniform( "uComposition" ), 0 );
	glUniform1f( sliceProgram.Uniform( "uOpacity" ), std::clamp( slice.opacity, 0.0f, 1.0f ) );
	const SoftEdge& e = slice.edge;
	glUniform4f( sliceProgram.Uniform( "uEdges" ), e.left, e.right, e.top, e.bottom );
	glUniform3f( sliceProgram.Uniform( "uRamp" ), e.power, e.luminance, e.gamma );
	glUniform1i( sliceProgram.Uniform( "uTestPattern" ), screen.testPattern ? 1 : 0 );
	glUniform3f( sliceProgram.Uniform( "uColour" ), colour[ 0 ], colour[ 1 ], colour[ 2 ] );
	glBindVertexArray( vao );
	glBindBuffer( GL_ARRAY_BUFFER, vbo );
	// Orphan and refill: the driver never waits for the previous slice's draw.
	glBufferData( GL_ARRAY_BUFFER, (GLsizeiptr)( vertices.size() * sizeof( float ) ), vertices.data(), GL_STREAM_DRAW );
	glDrawArrays( GL_TRIANGLES, 0, (GLsizei)( vertices.size() / 6 ) );
}

void OutputRenderer::Render( const AdvancedOutputSetup& setup, GLuint composition, int compWidth, int compHeight )
{
	Match( setup.screens.size() );
	const float identity[ 9 ] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
	for( size_t s = 0; s < setup.screens.size(); ++s )
	{
		const Screen& screen = setup.screens[ s ];
		ScreenState& state   = *screens[ s ];
		if( !state.target.Ensure( screen.width, screen.height ) || !state.finished.Ensure( screen.width, screen.height ) )
			continue;
		state.width  = screen.width;
		state.height = screen.height;
		ResetGlState();
		state.target.Bind();
		glClearColor( 0, 0, 0, 1 );
		glClear( GL_COLOR_BUFFER_BIT );
		if( screen.enabled )
		{
			glActiveTexture( GL_TEXTURE0 );
			glBindTexture( GL_TEXTURE_2D, composition );
			glEnable( GL_BLEND );
			glBlendFunc( GL_ONE, GL_ONE_MINUS_SRC_ALPHA );
			for( size_t i = 0; i < screen.slices.size(); ++i )
				if( screen.slices[ i ].enabled )
					DrawSlice( screen.slices[ i ], (int)i, screen, compWidth, compHeight );
			ResetGlState();
		}

		state.finished.Bind();
		glBindVertexArray( vao );
		finishProgram.Use();
		glUniformMatrix3fv( finishProgram.Uniform( "uTransform" ), 1, GL_FALSE, identity );
		glUniform4f( finishProgram.Uniform( "uTexRect" ), 0, 1, 1, 0 );
		glUniform1i( finishProgram.Uniform( "uTex0" ), 0 );
		glUniform1f( finishProgram.Uniform( "uOpacity" ), screen.enabled ? std::clamp( screen.opacity, 0.0f, 1.0f ) : 0.0f );
		glUniform1f( finishProgram.Uniform( "uBrightness" ), screen.brightness );
		glUniform1f( finishProgram.Uniform( "uContrast" ), screen.contrast );
		glUniform3f( finishProgram.Uniform( "uRgb" ), screen.red, screen.green, screen.blue );
		glActiveTexture( GL_TEXTURE0 );
		glBindTexture( GL_TEXTURE_2D, state.target.Texture() );
		glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
		ResetGlState();
	}
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
}

void OutputRenderer::Match( size_t count )
{
	while( screens.size() < count )
		screens.push_back( std::make_unique< ScreenState >() );
	screens.resize( count );
}

GLuint OutputRenderer::ScreenTexture( size_t index ) const
{
	return index < screens.size() ? screens[ index ]->finished.Texture() : 0;
}

void OutputRenderer::SyncWindows( const AdvancedOutputSetup& setup, GLFWwindow* main )
{
	Match( setup.screens.size() );
	bool vsyncTaken = false;
	for( size_t s = 0; s < setup.screens.size(); ++s )
	{
		const Screen& screen = setup.screens[ s ];
		ScreenState& state   = *screens[ s ];
		OutputDevice wanted  = screen.enabled ? screen.device : OutputDevice::Virtual;
		if( state.window && state.window->CloseRequested() )
		{
			// Closing an output window turns that screen virtual until it is chosen again.
			state.window.reset();
			continue;
		}
		bool changed = wanted != state.device || ( wanted == OutputDevice::Display && screen.display != state.display );
		if( wanted == OutputDevice::Display && state.missingDisplay && OutputWindow::FindDisplay( screen.display ) )
			changed = true; // the display came back
		if( !changed )
		{
			if( state.window )
				vsyncTaken = true;
			continue;
		}
		state.window.reset();
		state.device         = wanted;
		state.display        = screen.display;
		state.missingDisplay = false;
		if( wanted == OutputDevice::Virtual )
			continue;
		GLFWmonitor* monitor = nullptr;
		if( wanted == OutputDevice::Display )
		{
			monitor = OutputWindow::FindDisplay( screen.display );
			if( !monitor )
			{
				state.missingDisplay = true;
				continue;
			}
		}
		// Windows keep the screen's aspect, at most 1280 wide.
		float scale = std::min( 1.0f, 1280.0f / std::max( screen.width, 1 ) );
		state.window = std::make_unique< OutputWindow >();
		if( !state.window->Open( main, monitor, "STLT-VJ - " + screen.name, (int)( screen.width * scale ), (int)( screen.height * scale ), !vsyncTaken ) )
			state.window.reset();
		else
			vsyncTaken = true;
	}
	glfwMakeContextCurrent( main );
}

void OutputRenderer::CloseWindows()
{
	for( auto& state : screens )
	{
		state->window.reset();
		state->device = OutputDevice::Virtual;
	}
}

void OutputRenderer::Present( Compositor& compositor, GLFWwindow* main )
{
	for( auto& state : screens )
		if( state->window && state->window->IsOpen() )
			state->window->Present( compositor, state->finished.Texture(), state->width, state->height, main );
}

bool OutputRenderer::AnyWindowOpen() const
{
	for( const auto& state : screens )
		if( state->window && state->window->IsOpen() )
			return true;
	return false;
}

std::string OutputRenderer::DeviceStatus( size_t index ) const
{
	if( index >= screens.size() )
		return "";
	const ScreenState& state = *screens[ index ];
	if( state.missingDisplay )
		return "display \"" + state.display + "\" not connected";
	if( state.window && state.window->IsOpen() )
		return state.window->Description();
	return "virtual (not shown)";
}
}
