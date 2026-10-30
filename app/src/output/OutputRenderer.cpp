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

// Masks. The fill draws white triangles that invert what is under them: a fan over the outline then leaves
// every point covered an odd number of times white, which is the shape for any outline, even a crossing one.
const char* kMaskFillFragment = R"(
out vec4 fragColor;
void main()
{
	fragColor = vec4( 1.0 );
}
)";

// A Gaussian blur along uDirection, uRadius pixels each way (at most 48 taps, spaced out for wider ones).
const char* kMaskBlurFragment = R"(
uniform sampler2D uTex0;
uniform vec2 uDirection;
uniform float uRadius;
out vec4 fragColor;
void main()
{
	vec2 size = vec2( textureSize( uTex0, 0 ) );
	vec2 uv = gl_FragCoord.xy / size;
	int taps = int( min( ceil( uRadius ), 48.0 ) );
	float stride = max( uRadius / 48.0, 1.0 );
	float sigma = max( uRadius / 2.5, 0.4 );
	float sum = 0.0, total = 0.0;
	for( int i = -taps; i <= taps; ++i )
	{
		float x = float( i ) * stride;
		float w = exp( -0.5 * x * x / ( sigma * sigma ) );
		sum += w * texture( uTex0, uv + uDirection * x / size ).r;
		total += w;
	}
	fragColor = vec4( sum / total );
}
)";

// Darkens the screen by the coverage (or by what lies outside it): blended with ZERO, ONE_MINUS_SRC_ALPHA.
// The coverage may be smaller than the screen; it is stretched over it.
const char* kMaskApplyFragment = R"(
uniform sampler2D uTex0;
uniform vec2 uTargetSize;
uniform int uInvert;
uniform float uStrength;
out vec4 fragColor;
void main()
{
	float c = texture( uTex0, gl_FragCoord.xy / uTargetSize ).r;
	if( uInvert != 0 )
		c = 1.0 - c;
	fragColor = vec4( 0.0, 0.0, 0.0, c * uStrength );
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
	if( !maskFillProgram.Build( kSliceVertex, kMaskFillFragment ) || !maskBlurProgram.Build( kQuadVertexShader, kMaskBlurFragment )
	    || !maskApplyProgram.Build( kQuadVertexShader, kMaskApplyFragment ) )
	{
		error = "output mask shader " + maskFillProgram.Error() + maskBlurProgram.Error() + maskApplyProgram.Error();
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
	screens.clear(); // senders too, while the context is alive
	maskCoverage.Release();
	maskBlur.Release();
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

void OutputRenderer::ApplyMask( const Slice& mask, const Screen& screen, Fbo& target, GLuint composition )
{
	// A wide feather is smooth: its coverage is worked out at a half or a quarter of the screen size.
	int reduce = mask.feather >= 16 ? 4 : mask.feather >= 6 ? 2 : 1;
	int cw = std::max( screen.width / reduce, 1 ), ch = std::max( screen.height / reduce, 1 );
	std::vector< Vec2 > outline = mask.OutputOutline( 32 );
	if( outline.size() < 3 || mask.opacity <= 0 || !maskCoverage.Ensure( cw, ch ) || !maskBlur.Ensure( cw, ch ) )
		return;
	float W = (float)screen.width, H = (float)screen.height;
	vertices.clear();
	auto push = [ & ]( Vec2 p ) { vertices.insert( vertices.end(), { p.x / W * 2 - 1, 1 - p.y / H * 2, 0, 0, 0, 0 } ); };
	for( size_t i = 1; i + 1 < outline.size(); ++i )
	{
		push( outline[ 0 ] );
		push( outline[ i ] );
		push( outline[ i + 1 ] );
	}

	// Coverage: even-odd fill.
	maskCoverage.Bind();
	glClearColor( 0, 0, 0, 0 );
	glClear( GL_COLOR_BUFFER_BIT );
	glEnable( GL_BLEND );
	glBlendFunc( GL_ONE_MINUS_DST_COLOR, GL_ZERO );
	maskFillProgram.Use();
	glBindVertexArray( vao );
	glBindBuffer( GL_ARRAY_BUFFER, vbo );
	glBufferData( GL_ARRAY_BUFFER, (GLsizeiptr)( vertices.size() * sizeof( float ) ), vertices.data(), GL_STREAM_DRAW );
	glDrawArrays( GL_TRIANGLES, 0, (GLsizei)( vertices.size() / 6 ) );
	glDisable( GL_BLEND );

	// Feather (at least a pixel, which smooths the edge), across then down.
	const float identity[ 9 ] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
	maskBlurProgram.Use();
	glUniformMatrix3fv( maskBlurProgram.Uniform( "uTransform" ), 1, GL_FALSE, identity );
	glUniform4f( maskBlurProgram.Uniform( "uTexRect" ), 0, 0, 1, 1 );
	glUniform1i( maskBlurProgram.Uniform( "uTex0" ), 0 );
	glUniform1f( maskBlurProgram.Uniform( "uRadius" ), std::max( mask.feather / reduce, 1.0f ) );
	glActiveTexture( GL_TEXTURE0 );
	maskBlur.Bind();
	glUniform2f( maskBlurProgram.Uniform( "uDirection" ), 1, 0 );
	glBindTexture( GL_TEXTURE_2D, maskCoverage.Texture() );
	glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
	maskCoverage.Bind();
	glUniform2f( maskBlurProgram.Uniform( "uDirection" ), 0, 1 );
	glBindTexture( GL_TEXTURE_2D, maskBlur.Texture() );
	glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );

	// Darken the screen.
	target.Bind();
	glEnable( GL_BLEND );
	glBlendFunc( GL_ZERO, GL_ONE_MINUS_SRC_ALPHA );
	maskApplyProgram.Use();
	glUniformMatrix3fv( maskApplyProgram.Uniform( "uTransform" ), 1, GL_FALSE, identity );
	glUniform4f( maskApplyProgram.Uniform( "uTexRect" ), 0, 0, 1, 1 );
	glUniform1i( maskApplyProgram.Uniform( "uTex0" ), 0 );
	glUniform2f( maskApplyProgram.Uniform( "uTargetSize" ), (float)screen.width, (float)screen.height );
	glUniform1i( maskApplyProgram.Uniform( "uInvert" ), mask.invert ? 1 : 0 );
	glUniform1f( maskApplyProgram.Uniform( "uStrength" ), std::clamp( mask.opacity, 0.0f, 1.0f ) );
	glBindTexture( GL_TEXTURE_2D, maskCoverage.Texture() );
	glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );

	// Back to drawing slices.
	glBlendFunc( GL_ONE, GL_ONE_MINUS_SRC_ALPHA );
	glBindTexture( GL_TEXTURE_2D, composition );
}

void OutputRenderer::Render( const AdvancedOutputSetup& setup, GLuint composition, int compWidth, int compHeight, bool live )
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
			{
				const Slice& slice = screen.slices[ i ];
				if( !slice.enabled )
					continue;
				if( slice.mask )
					ApplyMask( slice, screen, state.target, composition );
				else
					DrawSlice( slice, (int)i, screen, compWidth, compHeight );
			}
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
		if( live )
			Share( screen, state );
		else
		{
			state.local.reset();
			state.ndi.reset();
			state.localName.clear();
			state.ndiName.clear();
			state.ndiError.clear();
		}
	}
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
}

void OutputRenderer::Share( const Screen& screen, ScreenState& state )
{
	std::string name = "STLT-VJ " + screen.name;
	bool local = screen.enabled && screen.share && LocalSharingName();
	if( !local || state.localName != name )
		state.local.reset();
	if( local )
	{
		if( !state.local )
		{
			state.local     = CreateLocalSender( name );
			state.localName = name;
		}
		if( state.local )
			state.local->Send( state.finished.Texture(), screen.width, screen.height );
		ResetGlState();
	}

	bool ndi = screen.enabled && screen.ndi;
	if( !ndi || state.ndiName != name )
	{
		state.ndi.reset();
		state.ndiError.clear();
		state.ndiName.clear();
	}
	if( ndi )
	{
		if( !state.ndi && state.ndiName.empty() )
		{
			state.ndi     = CreateNdiSender( name, state.ndiError );
			state.ndiName = name; // tried: a missing runtime is not looked for again every frame
		}
		if( state.ndi )
			state.ndi->Send( state.finished.Texture(), screen.width, screen.height );
	}
}

std::string OutputRenderer::SharingStatus( size_t index ) const
{
	if( index >= screens.size() )
		return "";
	const ScreenState& state = *screens[ index ];
	std::string status;
	if( state.local )
		status = state.local->Status();
	std::string ndi = state.ndi ? state.ndi->Status() : state.ndiError;
	if( !ndi.empty() )
		status += ( status.empty() ? "" : "\n" ) + ndi;
	return status;
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

void OutputRenderer::SyncWindows( const AdvancedOutputSetup& setup, GLFWwindow* main, bool live )
{
	Match( setup.screens.size() );
	bool vsyncTaken = false;
	for( size_t s = 0; s < setup.screens.size(); ++s )
	{
		const Screen& screen = setup.screens[ s ];
		ScreenState& state   = *screens[ s ];
		OutputDevice wanted  = live && screen.enabled ? screen.device : OutputDevice::Virtual;
		if( state.window && state.window->CloseRequested() )
		{
			// Closing an output window turns that screen virtual until it is chosen again.
			state.window.reset();
			windowClosed = true;
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
		return OutputWindow::DisplayLabel( state.display );
	if( state.window && state.window->IsOpen() )
		return state.window->Description();
	return "virtual (not shown)";
}
}
