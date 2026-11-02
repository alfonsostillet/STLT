#include "Compositor.h"
#include <algorithm>
#include <cmath>
#include "fx/Effect.h"

namespace stltvj
{
namespace
{
// Shading values match stltvj::Shading.
const char* kClipFragment = R"(
uniform sampler2D uTex0;
uniform sampler2D uTex1;
uniform sampler2D uTex2;
uniform int uShading;
uniform int uBt709;
uniform int uFullRange;
uniform float uOpacity;
in vec2 vUv;
out vec4 fragColor;

// HAP Q stores scaled YCoCg in DXT5 (Co in red, Cg in green, scale in blue, Y in alpha).
vec3 YCoCgToRgb( vec4 c )
{
	c += vec4( -0.50196078431373, -0.50196078431373, 0.0, 0.0 );
	float scale = c.z * ( 255.0 / 8.0 ) + 1.0;
	float co = c.x / scale;
	float cg = c.y / scale;
	float y = c.w;
	return vec3( y + co - cg, y + cg, y - co - cg );
}

// DXV High Quality: Co and Cg at half scale, with 127 for zero.
vec3 DxvYCoCgToRgb( float y, vec2 cocg )
{
	vec2 c = ( cocg - 127.0 / 255.0 ) * 2.0;
	return vec3( y + c.x - c.y, y + c.y, y - c.x - c.y );
}

// Planar YUV (H.264 and most video) to RGB, with the levels and matrix the file declares.
vec3 YuvToRgb( float y, float u, float v )
{
	u -= 0.5;
	v -= 0.5;
	if( uFullRange == 0 )
	{
		y = ( y - 16.0 / 255.0 ) * ( 255.0 / 219.0 );
		u *= 255.0 / 224.0;
		v *= 255.0 / 224.0;
	}
	if( uBt709 != 0 )
		return vec3( y + 1.5748 * v, y - 0.1873 * u - 0.4681 * v, y + 1.8556 * u );
	return vec3( y + 1.402 * v, y - 0.344136 * u - 0.714136 * v, y + 1.772 * u );
}

void main()
{
	vec4 c = texture( uTex0, vUv );
	vec4 rgba;
	if( uShading == 5 )
		rgba = vec4( YuvToRgb( c.r, texture( uTex1, vUv ).r, texture( uTex2, vUv ).r ), 1.0 );
	else if( uShading == 6 )
	{
		vec2 uv = texture( uTex1, vUv ).rg;
		rgba = vec4( YuvToRgb( c.r, uv.x, uv.y ), 1.0 );
	}
	else if( uShading == 0 )
		rgba = vec4( c.rgb, 1.0 );
	else if( uShading == 1 )
		rgba = c;
	else if( uShading == 2 )
		rgba = vec4( YCoCgToRgb( c ), 1.0 );
	else if( uShading == 3 )
		rgba = vec4( YCoCgToRgb( c ), texture( uTex1, vUv ).r );
	else if( uShading == 7 )
		rgba = vec4( c.a > 0.0 ? c.rgb / c.a : vec3( 0.0 ), c.a );
	else if( uShading == 8 )
		rgba = vec4( DxvYCoCgToRgb( c.r, texture( uTex1, vUv ).rg ), 1.0 );
	else if( uShading == 9 )
		rgba = vec4( DxvYCoCgToRgb( c.r, texture( uTex1, vUv ).rg ), c.g );
	else
		rgba = vec4( c.rrr, 1.0 );
	rgba = clamp( rgba, 0.0, 1.0 );
	float a = rgba.a * uOpacity;
	fragColor = vec4( rgba.rgb * a, a );
}
)";

// Blends a premultiplied layer over the opaque composition below it.
const char* kBlendFragment = R"(
uniform sampler2D uBase;
uniform sampler2D uLayer;
uniform int uMode;
uniform float uOpacity;
in vec2 vUv;
out vec4 fragColor;

vec3 Blend( vec3 b, vec3 l )
{
	if( uMode == 1 ) return b + l;
	if( uMode == 2 ) return 1.0 - ( 1.0 - b ) * ( 1.0 - l );
	if( uMode == 3 ) return b * l;
	if( uMode == 4 ) return max( b, l );
	if( uMode == 5 ) return min( b, l );
	if( uMode == 6 ) return abs( b - l );
	if( uMode == 7 ) return max( b - l, 0.0 );
	return l;
}

void main()
{
	vec3 base = texture( uBase, vUv ).rgb;
	vec4 layer = texture( uLayer, vUv ) * uOpacity;
	vec3 colour = layer.a > 0.0 ? layer.rgb / layer.a : vec3( 0.0 );
	if( uMode >= 8 )
	{
		// Luma key: the layer's brightness (or darkness) is how much of it shows.
		float luma = dot( colour, vec3( 0.2126, 0.7152, 0.0722 ) );
		float key  = uMode == 8 ? luma : 1.0 - luma;
		fragColor  = vec4( clamp( mix( base, colour, layer.a * key ), 0.0, 1.0 ), 1.0 );
		return;
	}
	fragColor = vec4( clamp( mix( base, Blend( base, colour ), layer.a ), 0.0, 1.0 ), 1.0 );
}
)";

const char* kCopyFragment = R"(
uniform sampler2D uTex0;
uniform float uOpacity;
in vec2 vUv;
out vec4 fragColor;
void main()
{
	fragColor = texture( uTex0, vUv ) * uOpacity;
}
)";

// From the outgoing clip (uFrom) to the incoming one (uTo), both premultiplied. Modes match TransitionMode.
const char* kTransitionFragment = R"(
uniform sampler2D uFrom;
uniform sampler2D uTo;
uniform int uMode;
uniform float uProgress;
in vec2 vUv;
out vec4 fragColor;
void main()
{
	vec4 a = texture( uFrom, vUv );
	vec4 b = texture( uTo, vUv );
	float t = clamp( uProgress, 0.0, 1.0 );
	if( uMode == 1 )
	{
		fragColor = t < 0.5 ? a * ( 1.0 - 2.0 * t ) : b * ( 2.0 * t - 1.0 );
		return;
	}
	if( uMode >= 2 )
	{
		// Wipes: the new clip slides its edge across, with a soft edge of 2 %.
		float x = uMode == 2 ? 1.0 - vUv.x : uMode == 3 ? vUv.x : uMode == 4 ? vUv.y : 1.0 - vUv.y;
		float edge = t * 1.02;
		t = 1.0 - smoothstep( edge - 0.02, edge, x );
	}
	fragColor = mix( a, b, t );
}
)";

void Identity( float m[ 9 ] )
{
	const float identity[ 9 ] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
	std::copy( identity, identity + 9, m );
}

// Places a w x h picture in a W x H target: fit mode, then scale, rotation and offset in target pixels (y up).
void PlaceClip( float m[ 9 ], float w, float h, float W, float H, FitMode fit, float scale, float degrees, float offsetX, float offsetY )
{
	float sx = 1, sy = 1;
	switch( fit )
	{
	case FitMode::Fit: sx = sy = std::min( W / w, H / h ); break;
	case FitMode::Fill: sx = sy = std::max( W / w, H / h ); break;
	case FitMode::Stretch:
		sx = W / w;
		sy = H / h;
		break;
	default: break;
	}
	float hw = w * sx * scale / 2, hh = h * sy * scale / 2;
	float r = degrees * 3.14159265f / 180.0f, c = std::cos( r ), s = std::sin( r );
	float nx = 2 / W, ny = 2 / H;
	// Column-major: x axis, y axis, translation.
	m[ 0 ] = c * hw * nx;
	m[ 1 ] = s * hw * ny;
	m[ 2 ] = 0;
	m[ 3 ] = -s * hh * nx;
	m[ 4 ] = c * hh * ny;
	m[ 5 ] = 0;
	m[ 6 ] = offsetX * nx;
	m[ 7 ] = offsetY * ny;
	m[ 8 ] = 1;
}

void SetTexRect( const Program& program, float u0, float v0, float u1, float v1 )
{
	glUniform4f( program.Uniform( "uTexRect" ), u0, v0, u1, v1 );
}
}

bool HasActiveEffects( const EffectChain& chain )
{
	for( const EffectSlot& slot : chain )
		if( slot.effect && !slot.bypass && !slot.effect->IsSource() )
			return true;
	return false;
}

bool Compositor::Init( std::string& error )
{
	if( !clipProgram.Build( kQuadVertexShader, kClipFragment ) )
	{
		error = "clip shader " + clipProgram.Error();
		return false;
	}
	if( !blendProgram.Build( kQuadVertexShader, kBlendFragment ) )
	{
		error = "blend shader " + blendProgram.Error();
		return false;
	}
	if( !copyProgram.Build( kQuadVertexShader, kCopyFragment ) )
	{
		error = "copy shader " + copyProgram.Error();
		return false;
	}
	if( !transitionProgram.Build( kQuadVertexShader, kTransitionFragment ) )
	{
		error = "transition shader " + transitionProgram.Error();
		return false;
	}
	transitionProgram.Use();
	glUniform1i( transitionProgram.Uniform( "uFrom" ), 0 );
	glUniform1i( transitionProgram.Uniform( "uTo" ), 1 );
	clipProgram.Use();
	glUniform1i( clipProgram.Uniform( "uTex0" ), 0 );
	glUniform1i( clipProgram.Uniform( "uTex1" ), 1 );
	glUniform1i( clipProgram.Uniform( "uTex2" ), 2 );
	blendProgram.Use();
	glUniform1i( blendProgram.Uniform( "uBase" ), 0 );
	glUniform1i( blendProgram.Uniform( "uLayer" ), 1 );
	copyProgram.Use();
	glUniform1i( copyProgram.Uniform( "uTex0" ), 0 );
	glUseProgram( 0 );
	glGenVertexArrays( 1, &vao );
	return true;
}

void Compositor::Shutdown()
{
	works.clear();
	canvasFinal.clear();
	final.Release();
	pool.clear();
	if( vao )
		glDeleteVertexArrays( 1, &vao );
	vao = 0;
}

Compositor::Pair& Compositor::Targets( int width, int height )
{
	std::unique_ptr< Pair >& pair = pool[ { width, height } ];
	if( !pair )
		pair = std::make_unique< Pair >();
	pair->a.Ensure( width, height );
	pair->b.Ensure( width, height );
	return *pair;
}

void Compositor::DrawClip( ClipPlayer& player, const float transform[ 9 ], float opacity )
{
	glBindVertexArray( vao );
	clipProgram.Use();
	glUniformMatrix3fv( clipProgram.Uniform( "uTransform" ), 1, GL_FALSE, transform );
	SetTexRect( clipProgram, 0, 0, player.UMax(), player.VMax() ); // HAP rows run top to bottom
	glUniform1i( clipProgram.Uniform( "uShading" ), (int)player.GetShading() );
	glUniform1f( clipProgram.Uniform( "uOpacity" ), opacity );
	glUniform1i( clipProgram.Uniform( "uBt709" ), player.Layout().bt709 ? 1 : 0 );
	glUniform1i( clipProgram.Uniform( "uFullRange" ), player.Layout().fullRange ? 1 : 0 );
	glActiveTexture( GL_TEXTURE2 );
	glBindTexture( GL_TEXTURE_2D, player.Texture( 2 ) );
	glActiveTexture( GL_TEXTURE1 );
	glBindTexture( GL_TEXTURE_2D, player.Texture( 1 ) );
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, player.Texture( 0 ) );
	glEnable( GL_BLEND );
	glBlendFunc( GL_ONE, GL_ONE_MINUS_SRC_ALPHA );
	glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
	ResetGlState();
}

// An FBO texture placed by `transform`, premultiplied over what is in the target.
void Compositor::DrawTexture( GLuint texture, const float transform[ 9 ], float opacity )
{
	glBindVertexArray( vao );
	copyProgram.Use();
	glUniformMatrix3fv( copyProgram.Uniform( "uTransform" ), 1, GL_FALSE, transform );
	SetTexRect( copyProgram, 0, 1, 1, 0 );
	glUniform1f( copyProgram.Uniform( "uOpacity" ), opacity );
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, texture );
	glEnable( GL_BLEND );
	glBlendFunc( GL_ONE, GL_ONE_MINUS_SRC_ALPHA );
	glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
	ResetGlState();
}

Fbo* Compositor::ApplyChain( EffectChain& chain, Fbo* source, Fbo* scratch, const FrameInfo& frame )
{
	float identity[ 9 ];
	Identity( identity );
	for( EffectSlot& slot : chain )
	{
		if( !slot.effect || slot.bypass || slot.effect->IsSource() || slot.mix <= 0 )
			continue;
		slot.effect->Update( frame );
		scratch->Bind();
		glClearColor( 0, 0, 0, 0 );
		glClear( GL_COLOR_BUFFER_BIT );
		ResetGlState();
		if( !slot.effect->Process( source->Texture(), source->Width(), source->Height(), scratch->Framebuffer(), frame ) )
			continue;
		if( slot.mix < 1 )
		{
			// Dry/wet: result = effect * mix + input * (1 - mix), blended straight into the effect's output.
			scratch->Bind();
			glBindVertexArray( vao );
			copyProgram.Use();
			glUniformMatrix3fv( copyProgram.Uniform( "uTransform" ), 1, GL_FALSE, identity );
			SetTexRect( copyProgram, 0, 1, 1, 0 );
			glUniform1f( copyProgram.Uniform( "uOpacity" ), 1.0f );
			glActiveTexture( GL_TEXTURE0 );
			glBindTexture( GL_TEXTURE_2D, source->Texture() );
			glEnable( GL_BLEND );
			glBlendColor( 0, 0, 0, 1 - slot.mix );
			glBlendFunc( GL_CONSTANT_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA );
			glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
			ResetGlState();
		}
		std::swap( source, scratch );
	}
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	return source;
}

Fbo* Compositor::RenderClipSource( Clip& clip, int compWidth, int compHeight, const FrameInfo& frame )
{
	bool effects = HasActiveEffects( clip.effects );
	if( clip.generator )
	{
		Pair& t = Targets( compWidth, compHeight );
		t.a.Bind();
		glClearColor( 0, 0, 0, 0 );
		glClear( GL_COLOR_BUFFER_BIT );
		ResetGlState();
		clip.generator->Update( frame );
		clip.generator->Process( 0, compWidth, compHeight, t.a.Framebuffer(), frame );
		return effects ? ApplyChain( clip.effects, &t.a, &t.b, frame ) : &t.a;
	}
	if( !effects || !clip.Loaded() || !clip.player->HasFrame() )
		return nullptr;
	ClipPlayer& player = *clip.player;
	Pair& t            = Targets( player.Width(), player.Height() );
	t.a.Bind();
	glClearColor( 0, 0, 0, 0 );
	glClear( GL_COLOR_BUFFER_BIT );
	float identity[ 9 ];
	Identity( identity );
	DrawClip( player, identity, 1.0f );
	return ApplyChain( clip.effects, &t.a, &t.b, frame );
}

// Draws the clip (with its effects) into `target`. forceFit ignores the clip's transform and draws on black
// (the preview monitor); otherwise the target is cleared to transparent (a layer).
void Compositor::DrawClipInto( Clip& clip, Fbo& target, const FrameInfo& frame, int compWidth, int compHeight, bool forceFit )
{
	float W = (float)target.Width(), H = (float)target.Height();
	// Opacity and transform as they are on this beat (beat animations).
	struct
	{
		float opacity, x, y, scale, rotation;
	} shown = { clip.Shown( ClipProperty::Opacity, frame.beat, W, H ), clip.Shown( ClipProperty::PositionX, frame.beat, W, H ),
		        clip.Shown( ClipProperty::PositionY, frame.beat, W, H ), clip.Shown( ClipProperty::Scale, frame.beat, W, H ),
		        clip.Shown( ClipProperty::Rotation, frame.beat, W, H ) };
	Fbo* source = RenderClipSource( clip, compWidth, compHeight, frame );
	target.Bind();
	glClearColor( 0, 0, 0, forceFit ? 1.0f : 0.0f );
	glClear( GL_COLOR_BUFFER_BIT );
	float transform[ 9 ];
	if( source )
	{
		// A generator fills the composition; a movie keeps its own size.
		float w = (float)source->Width(), h = (float)source->Height();
		if( forceFit )
			PlaceClip( transform, w, h, W, H, FitMode::Fit, 1, 0, 0, 0 );
		else
			PlaceClip( transform, w, h, W, H, clip.generator ? FitMode::Stretch : clip.fit, shown.scale, shown.rotation, shown.x, shown.y );
		DrawTexture( source->Texture(), transform, forceFit ? 1.0f : shown.opacity );
	}
	else if( clip.Loaded() && clip.player->HasFrame() )
	{
		ClipPlayer& player = *clip.player;
		float w = (float)player.Width(), h = (float)player.Height();
		if( forceFit )
			PlaceClip( transform, w, h, W, H, FitMode::Fit, 1, 0, 0, 0 );
		else
			PlaceClip( transform, w, h, W, H, clip.fit, shown.scale, shown.rotation, shown.x, shown.y );
		target.Bind();
		DrawClip( player, transform, forceFit ? 1.0f : shown.opacity );
	}
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
}

Compositor::Work& Compositor::WorkFor( int width, int height )
{
	std::unique_ptr< Work >& w = works[ { width, height } ];
	if( !w )
		w = std::make_unique< Work >();
	return *w;
}

GLuint Compositor::CanvasTexture( const std::string& id ) const
{
	auto it = canvasFinal.find( id );
	return it == canvasFinal.end() ? 0 : it->second->Texture();
}

Fbo* Compositor::Mix( std::vector< Source >& sources, Work& work, int W, int H, const FrameInfo& frame )
{
	if( !work.layer.Ensure( W, H ) || !work.scratch.Ensure( W, H ) || !work.mix[ 0 ].Ensure( W, H ) || !work.mix[ 1 ].Ensure( W, H ) )
		return nullptr;
	ResetGlState();
	int current = 0;
	work.mix[ current ].Bind();
	glClearColor( 0, 0, 0, 1 );
	glClear( GL_COLOR_BUFFER_BIT );
	auto ready = []( const Clip& clip ) { return clip.generator || ( clip.Loaded() && clip.player->HasFrame() ); };

	float identity[ 9 ];
	Identity( identity );
	for( Source& source : sources )
	{
		Clip& clip = *source.clip;
		if( source.outgoing )
		{
			// A transition: both clips, mixed into the layer.
			if( !work.from.Ensure( W, H ) || !work.to.Ensure( W, H ) )
				continue;
			DrawClipInto( *source.outgoing, work.from, frame, W, H, false );
			if( ready( clip ) )
				DrawClipInto( clip, work.to, frame, W, H, false );
			else
			{
				work.to.Bind();
				glClearColor( 0, 0, 0, 0 );
				glClear( GL_COLOR_BUFFER_BIT );
			}
			work.layer.Bind();
			glBindVertexArray( vao );
			transitionProgram.Use();
			glUniformMatrix3fv( transitionProgram.Uniform( "uTransform" ), 1, GL_FALSE, identity );
			SetTexRect( transitionProgram, 0, 1, 1, 0 );
			glUniform1i( transitionProgram.Uniform( "uMode" ), (int)source.transition );
			glUniform1f( transitionProgram.Uniform( "uProgress" ), source.progress );
			glActiveTexture( GL_TEXTURE1 );
			glBindTexture( GL_TEXTURE_2D, work.to.Texture() );
			glActiveTexture( GL_TEXTURE0 );
			glBindTexture( GL_TEXTURE_2D, work.from.Texture() );
			glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
			ResetGlState();
		}
		else
			DrawClipInto( clip, work.layer, frame, W, H, false );
		Fbo* layerResult = ApplyChain( *source.effects, &work.layer, &work.scratch, frame );
		if( source.solo )
			KeepSolo( *layerResult, W, H );

		work.mix[ 1 - current ].Bind();
		glBindVertexArray( vao );
		blendProgram.Use();
		glUniformMatrix3fv( blendProgram.Uniform( "uTransform" ), 1, GL_FALSE, identity );
		SetTexRect( blendProgram, 0, 1, 1, 0 );
		glUniform1i( blendProgram.Uniform( "uMode" ), (int)source.blend );
		glUniform1f( blendProgram.Uniform( "uOpacity" ), source.opacity );
		glActiveTexture( GL_TEXTURE1 );
		glBindTexture( GL_TEXTURE_2D, layerResult->Texture() );
		glActiveTexture( GL_TEXTURE0 );
		glBindTexture( GL_TEXTURE_2D, work.mix[ current ].Texture() );
		glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
		ResetGlState();
		current = 1 - current;
	}
	return &work.mix[ current ];
}

void Compositor::KeepSolo( Fbo& layer, int width, int height )
{
	if( !solo.Ensure( width, height ) )
		return;
	float identity[ 9 ];
	Identity( identity );
	solo.Bind();
	glClearColor( 0, 0, 0, 1 );
	glClear( GL_COLOR_BUFFER_BIT );
	glEnable( GL_BLEND );
	glBlendFunc( GL_ONE, GL_ONE_MINUS_SRC_ALPHA ); // premultiplied, over black
	glBindVertexArray( vao );
	copyProgram.Use();
	glUniformMatrix3fv( copyProgram.Uniform( "uTransform" ), 1, GL_FALSE, identity );
	SetTexRect( copyProgram, 0, 1, 1, 0 );
	glUniform1f( copyProgram.Uniform( "uOpacity" ), 1.0f );
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, layer.Texture() );
	glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
	ResetGlState();
	soloDrawn = true;
}

void Compositor::Finish( Fbo& from, Fbo& to, const Composition& composition )
{
	float identity[ 9 ];
	Identity( identity );
	to.Bind();
	glBindVertexArray( vao );
	copyProgram.Use();
	glUniformMatrix3fv( copyProgram.Uniform( "uTransform" ), 1, GL_FALSE, identity );
	SetTexRect( copyProgram, 0, 1, 1, 0 );
	glUniform1f( copyProgram.Uniform( "uOpacity" ), composition.bypass ? 0.0f : std::clamp( composition.master, 0.0f, 1.0f ) );
	glBindTexture( GL_TEXTURE_2D, from.Texture() );
	glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
	ResetGlState();
}

void Compositor::Render( Composition& composition, const FrameInfo& frame, bool timelineView, const std::vector< CanvasSize >& canvases )
{
	int W = composition.width, H = composition.height;
	if( !final.Ensure( W, H ) )
		return;
	ResetGlState();

	// What is drawn, bottom first: the deck's layers (Session view) or the timeline's video tracks (Arrangement),
	// each track into the composition or the canvas it is sent to.
	std::vector< Source > sources;
	std::vector< Source > soloOnly; // a track the monitor shows alone though it is not in the mix
	std::map< std::string, std::vector< Source > > canvasSources;
	soloDrawn = false;
	auto ready = []( const Clip& clip ) { return clip.generator || ( clip.Loaded() && clip.player->HasFrame() ); };
	auto isCanvas = [ & ]( const std::string& id ) {
		return !id.empty() && std::any_of( canvases.begin(), canvases.end(), [ & ]( const CanvasSize& c ) { return c.id == id; } );
	};
	if( timelineView )
	{
		Timeline& timeline = composition.timeline;
		bool anySolo       = std::any_of( timeline.tracks.begin(), timeline.tracks.end(), []( const TimelineTrack& t ) { return t.solo && !t.audio; } );
		for( int k = 0; k < (int)timeline.tracks.size(); ++k )
		{
			TimelineTrack& track = timeline.tracks[ k ];
			// The monitor's Layer view shows a track even when it is muted, soloed out or faded.
			bool hidden = track.mute || ( anySolo && !track.solo ) || track.opacity <= 0;
			if( hidden && k == soloLayer && !track.audio && !timeline.black )
			{
				int index = track.ClipAt( timeline.position );
				if( index >= 0 && ready( track.clips[ index ].clip ) )
					soloOnly.push_back( { &track.clips[ index ].clip, nullptr, 1, TransitionMode::Fade, 1, BlendMode::Alpha, &track.effects, true } );
			}
			if( track.audio || track.mute || ( anySolo && !track.solo ) || timeline.black || track.opacity <= 0 )
				continue;
			// The clip's own blend; its opacity and transform (keyframed) are its fields.
			int index = track.ClipAt( timeline.position );
			if( index >= 0 && ready( track.clips[ index ].clip ) )
			{
				Source source{ &track.clips[ index ].clip, nullptr, 1, TransitionMode::Fade, track.opacity, track.clips[ index ].blend, &track.effects,
					           k == soloLayer };
				( isCanvas( track.output ) ? canvasSources[ track.output ] : sources ).push_back( source );
			}
		}
	}
	else
	{
		bool anySolo = std::any_of( composition.layers.begin(), composition.layers.end(), []( const Layer& l ) { return l.solo; } );
		for( int index = 0; index < (int)composition.layers.size(); ++index )
		{
			Layer& layer  = composition.layers[ index ];
			float opacity = layer.ShownOpacity( frame.beat ) * layer.master * layer.CrossfadeGain( composition.crossfader );
			Clip* playing = composition.ActiveClip( index );
			if( layer.bypass || ( anySolo && !layer.solo ) || !playing || opacity <= 0 )
				continue;
			// The clip may sit in another row (a layer based launch).
			Clip* outgoing = layer.transitionProgress < 1 ? composition.OutgoingClip( index ) : nullptr;
			if( outgoing && !ready( *outgoing ) )
				outgoing = nullptr;
			if( !ready( *playing ) && !outgoing )
				continue;
			sources.push_back( { playing, outgoing, layer.transitionProgress, layer.transition, opacity, layer.blend, &layer.effects, index == soloLayer } );
		}
	}

	Work& work = WorkFor( W, H );
	if( !soloOnly.empty() )
		Mix( soloOnly, work, W, H, frame );
	Fbo* mixed = Mix( sources, work, W, H, frame );
	if( !mixed )
		return;
	// The composition's effects belong to the deck (Session view); the timeline is a show of its own, with effects
	// on its clips, so they leave it alone (STLT-CHASER on the deck's composition would mask the timeline).
	Fbo* result = timelineView ? mixed : ApplyChain( composition.effects, mixed, mixed == &work.mix[ 0 ] ? &work.mix[ 1 ] : &work.mix[ 0 ], frame );
	Finish( *result, final, composition );

	// The canvases: in the timeline, the tracks sent to them, at their own sizes (black when none is); outside it
	// (the deck's layers have no canvas) the composition stretched over them, so their screens keep showing.
	for( const CanvasSize& c : canvases )
	{
		if( c.width <= 0 || c.height <= 0 )
			continue;
		std::unique_ptr< Fbo >& out = canvasFinal[ c.id ];
		if( !out )
			out = std::make_unique< Fbo >();
		if( !out->Ensure( c.width, c.height ) )
			continue;
		if( !timelineView )
		{
			float identity[ 9 ];
			Identity( identity );
			out->Bind();
			glBindVertexArray( vao );
			copyProgram.Use();
			glUniformMatrix3fv( copyProgram.Uniform( "uTransform" ), 1, GL_FALSE, identity );
			SetTexRect( copyProgram, 0, 1, 1, 0 );
			glUniform1f( copyProgram.Uniform( "uOpacity" ), 1.0f ); // the composition has its master already
			glActiveTexture( GL_TEXTURE0 );
			glBindTexture( GL_TEXTURE_2D, final.Texture() );
			glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
			ResetGlState();
			continue;
		}
		std::vector< Source >& list = canvasSources[ c.id ];
		Fbo* canvas                 = Mix( list, WorkFor( c.width, c.height ), c.width, c.height, frame );
		if( canvas )
			Finish( *canvas, *out, composition );
	}
	// The layer the monitor shows alone, when it showed nothing: black.
	if( soloLayer >= 0 && !soloDrawn && solo.Ensure( W, H ) )
	{
		solo.Bind();
		glClearColor( 0, 0, 0, 1 );
		glClear( GL_COLOR_BUFFER_BIT );
	}
	// Canvases gone from the setup free their pictures, and sizes no longer mixed their targets.
	for( auto it = canvasFinal.begin(); it != canvasFinal.end(); )
		it = std::any_of( canvases.begin(), canvases.end(), [ & ]( const CanvasSize& c ) { return c.id == it->first; } ) ? std::next( it ) : canvasFinal.erase( it );
	for( auto it = works.begin(); it != works.end(); )
	{
		bool used = it->first == std::make_pair( W, H )
		            || ( timelineView && std::any_of( canvases.begin(), canvases.end(), [ & ]( const CanvasSize& c ) { return it->first == std::make_pair( c.width, c.height ); } ) );
		it = used ? std::next( it ) : works.erase( it );
	}
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
}

void Compositor::RenderClipAlone( Clip& clip, Fbo& target, int compWidth, int compHeight, const FrameInfo& frame )
{
	if( !target.Texture() )
		return;
	DrawClipInto( clip, target, frame, compWidth, compHeight, true );
}

void Compositor::MakeThumbnail( ClipPlayer& player )
{
	bool wasActive = player.Active();
	if( !player.Activate() )
		return;
	if( !player.HasFrame() )
		player.ShowNow( 0 );
	if( player.Thumbnail().Ensure( 160, 90 ) )
	{
		player.Thumbnail().Bind();
		glClearColor( 0, 0, 0, 1 );
		glClear( GL_COLOR_BUFFER_BIT );
		float transform[ 9 ];
		PlaceClip( transform, (float)player.Width(), (float)player.Height(), 160, 90, FitMode::Fit, 1, 0, 0, 0 );
		DrawClip( player, transform, 1.0f );
		glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	}
	if( !wasActive )
		player.Deactivate();
}

void Compositor::MakeGeneratorThumbnail( Clip& clip, const FrameInfo& frame )
{
	if( !clip.generator )
		return;
	if( !clip.generatorThumbnail )
		clip.generatorThumbnail = std::make_unique< Fbo >();
	if( !clip.generatorThumbnail->Ensure( 160, 90 ) )
		return;
	clip.generatorThumbnail->Bind();
	glClearColor( 0, 0, 0, 0 );
	glClear( GL_COLOR_BUFFER_BIT );
	ResetGlState();
	clip.generator->Update( frame );
	clip.generator->Process( 0, 160, 90, clip.generatorThumbnail->Framebuffer(), frame );
	ResetGlState();
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
}

void Compositor::DrawFitted( GLuint texture, int srcWidth, int srcHeight, int dstWidth, int dstHeight, float opacity )
{
	if( srcWidth <= 0 || srcHeight <= 0 || dstWidth <= 0 || dstHeight <= 0 )
		return;
	float transform[ 9 ];
	PlaceClip( transform, (float)srcWidth, (float)srcHeight, (float)dstWidth, (float)dstHeight, FitMode::Fit, 1, 0, 0, 0 );
	copyProgram.Use();
	glUniformMatrix3fv( copyProgram.Uniform( "uTransform" ), 1, GL_FALSE, transform );
	SetTexRect( copyProgram, 0, 1, 1, 0 );
	glUniform1f( copyProgram.Uniform( "uOpacity" ), opacity );
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, texture );
	glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
	glBindTexture( GL_TEXTURE_2D, 0 );
	glUseProgram( 0 );
}
}
