#include "Compositor.h"
#include <algorithm>
#include <cmath>

namespace stltvj
{
namespace
{
// Shading values match ClipPlayer::Shading.
const char* kClipFragment = R"(
uniform sampler2D uTex0;
uniform sampler2D uTex1;
uniform int uShading;
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

void main()
{
	vec4 c = texture( uTex0, vUv );
	vec4 rgba;
	if( uShading == 0 )
		rgba = vec4( c.rgb, 1.0 );
	else if( uShading == 1 )
		rgba = c;
	else if( uShading == 2 )
		rgba = vec4( YCoCgToRgb( c ), 1.0 );
	else if( uShading == 3 )
		rgba = vec4( YCoCgToRgb( c ), texture( uTex1, vUv ).r );
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
	clipProgram.Use();
	glUniform1i( clipProgram.Uniform( "uTex0" ), 0 );
	glUniform1i( clipProgram.Uniform( "uTex1" ), 1 );
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
	layerTarget.Release();
	mix[ 0 ].Release();
	mix[ 1 ].Release();
	final.Release();
	if( vao )
		glDeleteVertexArrays( 1, &vao );
	vao = 0;
}

void Compositor::DrawClip( ClipPlayer& player, const float transform[ 9 ], float opacity )
{
	clipProgram.Use();
	glUniformMatrix3fv( clipProgram.Uniform( "uTransform" ), 1, GL_FALSE, transform );
	SetTexRect( clipProgram, 0, 0, player.UMax(), player.VMax() ); // HAP rows run top to bottom
	glUniform1i( clipProgram.Uniform( "uShading" ), (int)player.GetShading() );
	glUniform1f( clipProgram.Uniform( "uOpacity" ), opacity );
	glActiveTexture( GL_TEXTURE1 );
	glBindTexture( GL_TEXTURE_2D, player.Texture( 1 ) );
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, player.Texture( 0 ) );
	glEnable( GL_BLEND );
	glBlendFunc( GL_ONE, GL_ONE_MINUS_SRC_ALPHA );
	glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
	glDisable( GL_BLEND );
}

void Compositor::Render( Composition& composition )
{
	int W = composition.width, H = composition.height;
	if( !layerTarget.Ensure( W, H ) || !mix[ 0 ].Ensure( W, H ) || !mix[ 1 ].Ensure( W, H ) || !final.Ensure( W, H ) )
		return;
	glBindVertexArray( vao );
	glDisable( GL_SCISSOR_TEST );
	glDisable( GL_DEPTH_TEST );
	glDisable( GL_CULL_FACE );

	int current = 0;
	mix[ current ].Bind();
	glClearColor( 0, 0, 0, 1 );
	glClear( GL_COLOR_BUFFER_BIT );

	bool anySolo = std::any_of( composition.layers.begin(), composition.layers.end(), []( const Layer& l ) { return l.solo; } );
	float identity[ 9 ];
	Identity( identity );
	for( Layer& layer : composition.layers )
	{
		if( layer.bypass || ( anySolo && !layer.solo ) || layer.active < 0 || layer.opacity <= 0 )
			continue;
		Clip& clip = layer.clips[ layer.active ];
		if( !clip.Loaded() || !clip.player->HasFrame() )
			continue;
		ClipPlayer& player = *clip.player;

		layerTarget.Bind();
		glClearColor( 0, 0, 0, 0 );
		glClear( GL_COLOR_BUFFER_BIT );
		float transform[ 9 ];
		PlaceClip( transform, (float)player.Video().width, (float)player.Video().height, (float)W, (float)H, clip.fit, clip.scale, clip.rotation,
		           clip.positionX, clip.positionY );
		DrawClip( player, transform, clip.opacity );

		mix[ 1 - current ].Bind();
		blendProgram.Use();
		glUniformMatrix3fv( blendProgram.Uniform( "uTransform" ), 1, GL_FALSE, identity );
		SetTexRect( blendProgram, 0, 1, 1, 0 );
		glUniform1i( blendProgram.Uniform( "uMode" ), (int)layer.blend );
		glUniform1f( blendProgram.Uniform( "uOpacity" ), layer.opacity );
		glActiveTexture( GL_TEXTURE1 );
		glBindTexture( GL_TEXTURE_2D, layerTarget.Texture() );
		glActiveTexture( GL_TEXTURE0 );
		glBindTexture( GL_TEXTURE_2D, mix[ current ].Texture() );
		glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
		current = 1 - current;
	}

	final.Bind();
	copyProgram.Use();
	glUniformMatrix3fv( copyProgram.Uniform( "uTransform" ), 1, GL_FALSE, identity );
	SetTexRect( copyProgram, 0, 1, 1, 0 );
	glUniform1f( copyProgram.Uniform( "uOpacity" ), std::clamp( composition.master, 0.0f, 1.0f ) );
	glBindTexture( GL_TEXTURE_2D, mix[ current ].Texture() );
	glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );

	glBindTexture( GL_TEXTURE_2D, 0 );
	glUseProgram( 0 );
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
}

void Compositor::RenderClipAlone( ClipPlayer& player, Fbo& target )
{
	if( !target.Texture() )
		return;
	glBindVertexArray( vao );
	target.Bind();
	glClearColor( 0, 0, 0, 1 );
	glClear( GL_COLOR_BUFFER_BIT );
	if( player.HasFrame() )
	{
		float transform[ 9 ];
		PlaceClip( transform, (float)player.Video().width, (float)player.Video().height, (float)target.Width(), (float)target.Height(), FitMode::Fit, 1, 0, 0,
		           0 );
		DrawClip( player, transform, 1.0f );
	}
	glUseProgram( 0 );
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
}

void Compositor::MakeThumbnail( ClipPlayer& player )
{
	bool wasActive = player.Active();
	if( !player.Activate() )
		return;
	if( !player.HasFrame() )
		player.ShowNow( 0 );
	if( player.Thumbnail().Ensure( 160, 90 ) )
		RenderClipAlone( player, player.Thumbnail() );
	if( !wasActive )
		player.Deactivate();
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
