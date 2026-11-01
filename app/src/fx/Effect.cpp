#include "Effect.h"
#include <algorithm>
#include <atomic>
#include <cmath>

namespace stltvj
{
const char* AnimModeName( AnimMode mode )
{
	static const char* names[] = { "Off", "Saw", "Ping Pong", "Sine", "Square", "Random", "Smooth Random", "Audio Level", "Audio Bass", "Audio Mid", "Audio Treble" };
	static_assert( sizeof( names ) / sizeof( names[ 0 ] ) == (size_t)AnimMode::Count, "a name for every mode" );
	return names[ std::clamp( (int)mode, 0, (int)AnimMode::Count - 1 ) ];
}

bool IsAudioMode( AnimMode mode )
{
	return mode >= AnimMode::AudioLevel && mode <= AnimMode::AudioTreble;
}

namespace
{
AnimationClock animationClock;
std::atomic< double > audioAskedAt{ -1e9 };
}

void SetAnimationClock( const AnimationClock& clock )
{
	animationClock = clock;
}

const AnimationClock& CurrentAnimationClock()
{
	return animationClock;
}

bool AudioAnimationWanted( double seconds )
{
	return animationClock.seconds - audioAskedAt.load() < seconds;
}

bool ParamInfo::IsAnimatable() const
{
	switch( kind )
	{
	case ParamKind::Text:
	case ParamKind::File:
	case ParamKind::Event:
	case ParamKind::Buffer: return false;
	default: return true;
	}
}

static float Hash( double n )
{
	double x = std::sin( n * 12.9898 + 78.233 ) * 43758.5453;
	return (float)( x - std::floor( x ) );
}

float Animation::Evaluate( double beat ) const
{
	double period = std::max( 1.0 / 64, (double)beats );
	double cycles = ( seconds ? animationClock.seconds : beat ) / period;
	float phase   = (float)( cycles - std::floor( cycles ) );
	float t       = 0;
	if( IsAudioMode( mode ) )
		audioAskedAt = animationClock.seconds;
	switch( mode )
	{
	case AnimMode::SmoothRandom:
	{
		float a = Hash( std::floor( cycles ) ), b = Hash( std::floor( cycles ) + 1 );
		t       = a + ( b - a ) * ( phase * phase * ( 3 - 2 * phase ) );
		break;
	}
	case AnimMode::AudioLevel:
	case AnimMode::AudioBass:
	case AnimMode::AudioMid:
	case AnimMode::AudioTreble: t = std::clamp( animationClock.audio[ (int)mode - (int)AnimMode::AudioLevel ], 0.0f, 1.0f ); break;
	case AnimMode::Saw: t = phase; break;
	case AnimMode::PingPong: t = phase < 0.5f ? phase * 2 : 2 - phase * 2; break;
	case AnimMode::Sine: t = 0.5f - 0.5f * std::cos( phase * 6.2831853f ); break;
	case AnimMode::Square: t = phase < 0.5f ? 0.0f : 1.0f; break;
	case AnimMode::Random: t = Hash( std::floor( cycles ) ); break;
	default: return from;
	}
	if( reverse )
		t = 1 - t;
	return from + ( to - from ) * t;
}

int Effect::FindParam( const std::string& name ) const
{
	for( size_t i = 0; i < params.size(); ++i )
		if( params[ i ].name == name )
			return (int)i;
	return -1;
}

void Effect::InitValues()
{
	size_t n = params.size();
	values.assign( n, 0 );
	effective.assign( n, 0 );
	pushed.assign( n, NAN );
	texts.assign( n, std::string() );
	animations.assign( n, Animation{} );
	eventFrames.assign( n, 0 );
	ResetToDefaults();
}

void Effect::ResetToDefaults()
{
	for( size_t i = 0; i < params.size(); ++i )
	{
		values[ i ]    = params[ i ].defaultValue;
		effective[ i ] = values[ i ];
		texts[ i ]     = params[ i ].defaultText;
		animations[ i ] = Animation{};
		if( params[ i ].IsText() )
			PushText( (int)i, texts[ i ] );
	}
}

void Effect::ForgetPushed()
{
	std::fill( pushed.begin(), pushed.end(), NAN );
	for( size_t i = 0; i < params.size(); ++i )
		if( params[ i ].IsText() )
			PushText( (int)i, texts[ i ] );
}

void Effect::SetValue( int index, float value )
{
	if( index < 0 || index >= (int)params.size() )
		return;
	const ParamInfo& p = params[ index ];
	if( p.kind == ParamKind::Bool || p.kind == ParamKind::Event )
		value = value >= 0.5f ? 1.0f : 0.0f;
	else if( p.kind == ParamKind::Integer )
		value = std::round( value );
	if( p.kind != ParamKind::Option )
		value = std::clamp( value, std::min( p.min, p.max ), std::max( p.min, p.max ) );
	values[ index ] = value;
}

void Effect::SetText( int index, const std::string& text )
{
	if( index < 0 || index >= (int)params.size() )
		return;
	texts[ index ] = text;
	PushText( index, text );
}

void Effect::Trigger( int index )
{
	if( index < 0 || index >= (int)params.size() )
		return;
	values[ index ]      = 1;
	eventFrames[ index ] = 2; // on for this frame, off on the next
}

void Effect::Update( const FrameInfo& frame )
{
	PollChanges();
	for( size_t i = 0; i < params.size(); ++i )
	{
		const ParamInfo& p = params[ i ];
		if( p.IsText() || p.kind == ParamKind::Buffer )
			continue;
		if( p.kind == ParamKind::Event && eventFrames[ i ] > 0 && --eventFrames[ i ] == 0 )
			values[ i ] = 0;

		float v                 = values[ i ];
		const Animation& anim   = animations[ i ];
		if( anim.mode != AnimMode::Off && p.IsAnimatable() )
		{
			float t = anim.Evaluate( frame.beat );
			if( p.kind == ParamKind::Option && !p.optionValues.empty() )
			{
				int last = (int)p.optionValues.size() - 1;
				v        = p.optionValues[ std::clamp( (int)std::floor( t * ( last + 1 ) ), 0, last ) ];
			}
			else if( p.kind == ParamKind::Bool )
				v = t >= 0.5f ? 1.0f : 0.0f;
			else
			{
				v = p.min + ( p.max - p.min ) * t;
				if( p.kind == ParamKind::Integer )
					v = std::round( v );
			}
		}
		effective[ i ] = v;
		if( !( pushed[ i ] == v ) )
		{
			pushed[ i ] = v;
			PushValue( (int)i, v );
		}
	}
}

void ResetGlState()
{
	glUseProgram( 0 );
	glBindVertexArray( 0 );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
	glBindBuffer( GL_PIXEL_UNPACK_BUFFER, 0 );
	glBindBuffer( GL_PIXEL_PACK_BUFFER, 0 );
	glBindBuffer( GL_UNIFORM_BUFFER, 0 );
	for( int unit = 3; unit >= 0; --unit )
	{
		glActiveTexture( GL_TEXTURE0 + unit );
		glBindTexture( GL_TEXTURE_2D, 0 );
	}
	glDisable( GL_BLEND );
	glBlendFunc( GL_ONE, GL_ZERO );
	glBlendEquation( GL_FUNC_ADD );
	glDisable( GL_DEPTH_TEST );
	glDepthMask( GL_TRUE );
	glDisable( GL_CULL_FACE );
	glDisable( GL_SCISSOR_TEST );
	glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
}
}
