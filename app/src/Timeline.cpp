#include "Composition.h"
#include <cmath>
#include <cstdio>
#include "audio/AudioEngine.h"

namespace stltvj
{
const char* BlockEndName( BlockEnd end )
{
	static const char* names[] = { "Next", "Loop", "Hold", "Black" };
	return names[ std::clamp( (int)end, 0, (int)BlockEnd::Count - 1 ) ];
}

const char* KeyCurveName( KeyCurve curve )
{
	static const char* names[] = { "Linear", "Ease In", "Ease Out", "Ease In Out", "Hold" };
	return names[ std::clamp( (int)curve, 0, (int)KeyCurve::Count - 1 ) ];
}

float CurveAt( KeyCurve curve, float f )
{
	f = std::clamp( f, 0.0f, 1.0f );
	switch( curve )
	{
	case KeyCurve::EaseIn: return f * f * f;
	case KeyCurve::EaseOut: return 1 - ( 1 - f ) * ( 1 - f ) * ( 1 - f );
	case KeyCurve::EaseInOut: return f < 0.5f ? 4 * f * f * f : 1 - 4 * ( 1 - f ) * ( 1 - f ) * ( 1 - f );
	case KeyCurve::Hold: return 0;
	default: return f;
	}
}

const char* KeyPropertyName( KeyProperty property )
{
	static const char* names[] = { "opacity", "positionX", "positionY", "scale", "rotation", "volume" };
	return names[ std::clamp( (int)property, 0, (int)KeyProperty::Count - 1 ) ];
}

float& TimelineClip::Field( KeyProperty property )
{
	switch( property )
	{
	case KeyProperty::Opacity: return clip.opacity;
	case KeyProperty::PositionX: return clip.positionX;
	case KeyProperty::PositionY: return clip.positionY;
	case KeyProperty::Scale: return clip.scale;
	case KeyProperty::Rotation: return clip.rotation;
	default: return clip.volume;
	}
}

float TimelineClip::Evaluate( KeyProperty property, double local ) const
{
	return EvaluateKeys( keys[ (int)property ], local, Field( property ) );
}

float EvaluateKeys( const std::vector< Keyframe >& k, double local, float fallback )
{
	if( k.empty() )
		return fallback;
	if( local <= k.front().time )
		return k.front().value;
	if( local >= k.back().time )
		return k.back().value;
	for( size_t i = 1; i < k.size(); ++i )
		if( local < k[ i ].time )
		{
			double span = k[ i ].time - k[ i - 1 ].time;
			float f     = CurveAt( k[ i - 1 ].curve, span > 0 ? (float)( ( local - k[ i - 1 ].time ) / span ) : 1.0f );
			return k[ i - 1 ].value + ( k[ i ].value - k[ i - 1 ].value ) * f;
		}
	return k.back().value;
}

int KeyIndex( const std::vector< Keyframe >& k, double local )
{
	for( int i = 0; i < (int)k.size(); ++i )
		if( std::fabs( k[ i ].time - local ) < 1e-3 )
			return i;
	return -1;
}

void SetKeyIn( std::vector< Keyframe >& k, double local, float value )
{
	int at = KeyIndex( k, local );
	if( at >= 0 )
	{
		k[ at ].value = value;
		return;
	}
	k.push_back( { local, value, KeyCurve::Linear } );
	std::stable_sort( k.begin(), k.end(), []( const Keyframe& a, const Keyframe& b ) { return a.time < b.time; } );
}

int TimelineClip::KeyAt( KeyProperty property, double local ) const
{
	return KeyIndex( keys[ (int)property ], local );
}

void TimelineClip::SetKey( KeyProperty property, double local, float value )
{
	SetKeyIn( keys[ (int)property ], local, value );
}

bool TimelineClip::RemoveKey( KeyProperty property, double local )
{
	int at = KeyAt( property, local );
	if( at < 0 )
		return false;
	keys[ (int)property ].erase( keys[ (int)property ].begin() + at );
	return true;
}

void TimelineClip::ShiftKeys( double seconds )
{
	for( std::vector< Keyframe >& k : keys )
		for( Keyframe& key : k )
			key.time -= seconds;
	for( ParamKeys& p : paramKeys )
		for( Keyframe& key : p.keys )
			key.time -= seconds;
}

ParamKeys* TimelineClip::FindParamKeys( int slot, const std::string& param, bool create )
{
	for( ParamKeys& p : paramKeys )
		if( p.slot == slot && p.param == param )
			return &p;
	if( !create )
		return nullptr;
	paramKeys.push_back( { slot, param, {} } );
	return &paramKeys.back();
}

void TimelineClip::CopyLookFrom( const TimelineClip& from )
{
	blend = from.blend;
	for( int p = 0; p < (int)KeyProperty::Count; ++p )
		keys[ p ] = from.keys[ p ];
	paramKeys = from.paramKeys;
}

namespace
{
template< typename F >
void ForEachKeyList( TimelineClip& c, F&& f )
{
	for( std::vector< Keyframe >& k : c.keys )
		f( k );
	for( ParamKeys& p : c.paramKeys )
		f( p.keys );
}

void SortKeys( std::vector< Keyframe >& k )
{
	std::stable_sort( k.begin(), k.end(), []( const Keyframe& a, const Keyframe& b ) { return a.time < b.time; } );
}
}

std::vector< double > TimelineClip::KeyTimes() const
{
	std::vector< double > times;
	ForEachKeyList( const_cast< TimelineClip& >( *this ), [ & ]( std::vector< Keyframe >& k ) {
		for( const Keyframe& key : k )
			times.push_back( key.time );
	} );
	std::sort( times.begin(), times.end() );
	std::vector< double > unique;
	for( double t : times )
		if( unique.empty() || t - unique.back() >= 1e-3 )
			unique.push_back( t );
	return unique;
}

void TimelineClip::MoveKeysAt( double from, double to )
{
	ForEachKeyList( *this, [ & ]( std::vector< Keyframe >& k ) {
		// One keyframe a list at a time: one already at `to` is replaced.
		int moving = -1;
		for( int i = 0; i < (int)k.size(); ++i )
			if( std::fabs( k[ i ].time - from ) < 1e-3 )
				moving = i;
		if( moving < 0 )
			return;
		Keyframe key = k[ moving ];
		k.erase( k.begin() + moving );
		k.erase( std::remove_if( k.begin(), k.end(), [ & ]( const Keyframe& other ) { return std::fabs( other.time - to ) < 1e-3; } ), k.end() );
		key.time = to;
		k.push_back( key );
		SortKeys( k );
	} );
}

void TimelineClip::RemoveKeysAt( double local )
{
	ForEachKeyList( *this, [ & ]( std::vector< Keyframe >& k ) {
		k.erase( std::remove_if( k.begin(), k.end(), [ & ]( const Keyframe& key ) { return std::fabs( key.time - local ) < 1e-3; } ), k.end() );
	} );
	paramKeys.erase( std::remove_if( paramKeys.begin(), paramKeys.end(), []( const ParamKeys& p ) { return p.keys.empty(); } ), paramKeys.end() );
}

void TimelineClip::SetCurveAt( double local, KeyCurve curve )
{
	ForEachKeyList( *this, [ & ]( std::vector< Keyframe >& k ) {
		for( Keyframe& key : k )
			if( std::fabs( key.time - local ) < 1e-3 )
				key.curve = curve;
	} );
}

void TimelineClip::Apply( double t )
{
	for( int p = 0; p < (int)KeyProperty::Count; ++p )
		if( !keys[ p ].empty() )
			Field( (KeyProperty)p ) = Evaluate( (KeyProperty)p, t - start );
	for( const ParamKeys& p : paramKeys )
	{
		if( p.keys.empty() || p.slot < 0 || p.slot >= (int)clip.effects.size() )
			continue;
		if( p.param == kMixParam )
		{
			clip.effects[ p.slot ].mix = std::clamp( EvaluateKeys( p.keys, t - start, clip.effects[ p.slot ].mix ), 0.0f, 1.0f );
			continue;
		}
		if( !clip.effects[ p.slot ].effect )
			continue;
		Effect& effect = *clip.effects[ p.slot ].effect;
		int index      = effect.FindParam( p.param );
		if( index >= 0 )
			effect.SetValue( index, EvaluateKeys( p.keys, t - start, effect.Value( index ) ) );
	}
}

double TimelineClip::MediaDuration() const
{
	if( clip.Loaded() )
		return clip.Duration();
	if( clip.audio )
		return clip.audio->Duration();
	return 0;
}

double TimelineClip::MediaTime( double t ) const
{
	double in       = std::max( 0.0, clip.inPoint );
	double duration = MediaDuration();
	double out      = clip.outPoint >= 0 ? clip.outPoint : duration;
	double time     = in + std::max( 0.0, t - start ) * std::max( 0.0f, clip.speed );
	if( duration <= 0 || out <= in || time < out )
		return time;
	if( clip.playMode == PlayMode::Loop )
		return in + std::fmod( time - in, out - in );
	return std::max( in, out - 1e-4 ); // holds the last frame
}

float TimelineTrack::VolumeAt( double t ) const
{
	if( volumeKeys.empty() )
		return volume;
	return std::clamp( EvaluateKeys( volumeKeys, t, volume ), 0.0f, 1.0f );
}

int TimelineTrack::ClipAt( double t ) const
{
	for( int i = 0; i < (int)clips.size(); ++i )
		if( t >= clips[ i ].start && t < clips[ i ].End() )
			return i;
	return -1;
}

double Timeline::Length() const
{
	double length = 0;
	for( const TimelineTrack& track : tracks )
		for( const TimelineClip& clip : track.clips )
			length = std::max( length, clip.End() );
	for( const TimelineBlock& block : blocks )
		length = std::max( length, block.end );
	return length;
}

int Timeline::BlockAt( double t ) const
{
	for( int i = 0; i < (int)blocks.size(); ++i )
		if( t >= blocks[ i ].start && t < blocks[ i ].end )
			return i;
	return -1;
}

void Timeline::Advance( double seconds )
{
	if( !playing || seconds <= 0 )
		return;
	double next = position + seconds;
	int block   = BlockAt( position );
	if( block >= 0 && next >= blocks[ block ].end )
	{
		const TimelineBlock& b = blocks[ block ];
		double length          = b.end - b.start;
		switch( b.endAction )
		{
		case BlockEnd::Next: position = next; break;
		case BlockEnd::Loop: position = length > 0 ? b.start + std::fmod( next - b.start, length ) : b.start; break;
		case BlockEnd::Hold:
			position = std::max( b.start, b.end - 1e-6 );
			playing  = false;
			break;
		case BlockEnd::Black:
			position = std::max( b.start, b.end - 1e-6 );
			playing  = false;
			black    = true;
			break;
		default: position = next; break;
		}
	}
	else
		position = next;
	// The end of the show: it stops there.
	double length = Length();
	if( position >= length )
	{
		position = length;
		playing  = false;
	}
}

void Timeline::Cue( int block )
{
	if( block < 0 || block >= (int)blocks.size() )
		return;
	position = blocks[ block ].start;
	playing  = true;
	black    = false;
}

void Timeline::Locate( double t )
{
	position = std::max( 0.0, t );
	black    = false;
}

void Timeline::SortBlocks()
{
	std::stable_sort( blocks.begin(), blocks.end(), []( const TimelineBlock& a, const TimelineBlock& b ) { return a.start < b.start; } );
}

int Timeline::Place( int track, TimelineClip placed )
{
	if( track < 0 || track >= (int)tracks.size() )
		return -1;
	std::vector< TimelineClip >& clips = tracks[ track ].clips;
	double s = placed.start, e = placed.End();
	std::vector< TimelineClip > kept;
	for( TimelineClip& c : clips )
	{
		if( c.End() <= s || c.start >= e )
			kept.push_back( std::move( c ) ); // untouched
		else if( c.start < s && c.End() > e )
		{
			// The new clip lands inside it: a head before and a tail after.
			TimelineClip tail;
			tail.clip   = CopyClip( c.clip );
			tail.start  = e;
			tail.length = c.End() - e;
			tail.CopyLookFrom( c );
			tail.ShiftKeys( e - c.start );
			tail.clip.inPoint += ( e - c.start ) * std::max( 0.0f, c.clip.speed );
			c.length = s - c.start;
			kept.push_back( std::move( c ) );
			kept.push_back( std::move( tail ) );
		}
		else if( c.start < s )
		{
			c.length = s - c.start; // cut short
			kept.push_back( std::move( c ) );
		}
		else if( c.End() > e )
		{
			// Its start is covered: it starts later, and further into its media (and its keyframes stay where they were in time).
			c.clip.inPoint += ( e - c.start ) * std::max( 0.0f, c.clip.speed );
			c.ShiftKeys( e - c.start );
			c.length = c.End() - e;
			c.start  = e;
			kept.push_back( std::move( c ) );
		}
		// else: covered entirely, removed
	}
	kept.push_back( std::move( placed ) );
	std::stable_sort( kept.begin(), kept.end(), []( const TimelineClip& a, const TimelineClip& b ) { return a.start < b.start; } );
	clips = std::move( kept );
	for( int i = 0; i < (int)clips.size(); ++i )
		if( clips[ i ].start == s && clips[ i ].End() == e )
			return i;
	return -1;
}

bool Timeline::Split( int track, int index, double t )
{
	if( track < 0 || track >= (int)tracks.size() || index < 0 || index >= (int)tracks[ track ].clips.size() )
		return false;
	std::vector< TimelineClip >& clips = tracks[ track ].clips;
	TimelineClip& c                    = clips[ index ];
	if( t <= c.start + 1e-3 || t >= c.End() - 1e-3 )
		return false;
	TimelineClip tail;
	tail.clip   = CopyClip( c.clip );
	tail.start  = t;
	tail.length = c.End() - t;
	tail.CopyLookFrom( c );
	tail.ShiftKeys( t - c.start );
	tail.clip.inPoint += ( t - c.start ) * std::max( 0.0f, c.clip.speed );
	c.length = t - c.start;
	clips.insert( clips.begin() + index + 1, std::move( tail ) );
	return true;
}

std::string Timeline::Timecode( double t ) const
{
	double rate = std::max( 1.0, fps );
	long long frames = (long long)std::floor( std::max( 0.0, t ) * rate + 1e-6 );
	long long perSecond = (long long)std::llround( rate );
	long long ff = frames % perSecond, seconds = frames / perSecond;
	char text[ 64 ];
	std::snprintf( text, sizeof( text ), "%02lld:%02lld:%02lld:%02lld", seconds / 3600, seconds / 60 % 60, seconds % 60, ff );
	return text;
}

std::string Timeline::BarsBeats( double t ) const
{
	double beats = std::max( 0.0, t ) / SecondsPerBeat();
	int perBar   = std::max( beatsPerBar, 1 );
	long long whole = (long long)std::floor( beats + 1e-9 );
	char text[ 64 ];
	std::snprintf( text, sizeof( text ), "%lld.%lld", whole / perBar + 1, whole % perBar + 1 );
	return text;
}
}
