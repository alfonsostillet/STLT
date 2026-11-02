#include "control/Timecode.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace stltvj
{
const char* TimecodeRateName( TimecodeRate rate )
{
	switch( rate )
	{
	case TimecodeRate::Fps24: return "24 fps";
	case TimecodeRate::Fps25: return "25 fps";
	case TimecodeRate::Fps2997Drop: return "29.97 fps drop frame";
	default: return "30 fps";
	}
}

double TimecodeRateFps( TimecodeRate rate )
{
	switch( rate )
	{
	case TimecodeRate::Fps24: return 24;
	case TimecodeRate::Fps25: return 25;
	case TimecodeRate::Fps2997Drop: return 30000.0 / 1001.0;
	default: return 30;
	}
}

int TimecodeRateFrames( TimecodeRate rate )
{
	switch( rate )
	{
	case TimecodeRate::Fps24: return 24;
	case TimecodeRate::Fps25: return 25;
	default: return 30;
	}
}

// The rate in the two bits MTC and Art-Net carry.
static TimecodeRate RateFromBits( int bits )
{
	static const TimecodeRate rates[] = { TimecodeRate::Fps24, TimecodeRate::Fps25, TimecodeRate::Fps2997Drop, TimecodeRate::Fps30 };
	return rates[ bits & 3 ];
}

double Timecode::Seconds() const
{
	int whole = ( hours * 60 + minutes ) * 60 + seconds;
	if( rate == TimecodeRate::Fps2997Drop )
	{
		int totalMinutes = hours * 60 + minutes;
		int64_t frame    = (int64_t)whole * 30 + frames - 2 * ( totalMinutes - totalMinutes / 10 );
		return (double)frame * 1001.0 / 30000.0;
	}
	return whole + frames / TimecodeRateFps( rate );
}

std::string Timecode::Text() const
{
	char text[ 32 ];
	std::snprintf( text, sizeof( text ), "%02d:%02d:%02d%c%02d", hours, minutes, seconds, rate == TimecodeRate::Fps2997Drop ? ';' : ':', frames );
	return text;
}

bool ParseTimecode( const std::string& text, Timecode& timecode )
{
	int parts[ 4 ] = {}, count = 0, digits = 0;
	bool drop      = false;
	for( char c : text )
	{
		if( c >= '0' && c <= '9' )
		{
			if( count >= 4 || ++digits > 2 )
				return false;
			parts[ count ] = parts[ count ] * 10 + ( c - '0' );
		}
		else if( c == ':' || c == ';' || c == '.' )
		{
			if( !digits || ++count >= 4 )
				return false;
			digits = 0;
			drop   = drop || c == ';';
		}
		else if( c != ' ' )
			return false;
	}
	if( count != 3 || !digits || parts[ 1 ] > 59 || parts[ 2 ] > 59 || parts[ 0 ] > 23 || parts[ 3 ] > 29 )
		return false;
	timecode.hours   = parts[ 0 ];
	timecode.minutes = parts[ 1 ];
	timecode.seconds = parts[ 2 ];
	timecode.frames  = parts[ 3 ];
	if( drop )
		timecode.rate = TimecodeRate::Fps2997Drop;
	return true;
}

void TimecodeReceiver::Reset()
{
	std::lock_guard< std::mutex > lock( mutex );
	lastTime = movedAt = -1;
	running  = false;
}

void TimecodeReceiver::Receive( const Timecode& timecode, double time, bool runningSource )
{
	std::lock_guard< std::mutex > lock( mutex );
	double s     = timecode.Seconds();
	double frame = 1.0 / TimecodeRateFps( timecode.rate );
	if( !runningSource )
		running = false; // a locate
	else if( lastTime >= 0 )
	{
		double moved = s - lastSeconds;
		if( moved > frame * 0.5 && moved < 1.0 )
		{
			running = true; // on the way forward
			movedAt = time;
		}
		else if( std::fabs( moved ) <= frame * 0.5 )
		{
			if( movedAt < 0 || time - movedAt > frame * 2.5 )
				running = false; // the same time again and again: stopped
		}
		else
			running = false; // a jump: running again from the next time on
	}
	last        = timecode;
	lastSeconds = s;
	lastTime    = time;
}

TimecodeReceiver::State TimecodeReceiver::At( double now, double freewheel ) const
{
	std::lock_guard< std::mutex > lock( mutex );
	State state;
	state.last = last;
	if( lastTime < 0 )
		return state;
	double frame  = 1.0 / TimecodeRateFps( last.rate );
	double age    = std::max( 0.0, now - lastTime );
	state.present = age < std::max( freewheel, 1.0 );
	state.running = running && age < std::max( freewheel, frame * 3 );
	state.seconds = lastSeconds + ( state.running ? age : 0 );
	return state;
}

// LTC ------------------------------------------------------------------------------------------------------

namespace
{
// The sync word that ends each 80-bit frame, as the bits arrive (bits 64 to 79).
const uint8_t kSync[ 16 ] = { 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1 };

int Field( const std::array< uint8_t, 80 >& bits, int first, int count )
{
	int value = 0;
	for( int i = 0; i < count; ++i )
		value |= bits[ first + i ] << i;
	return value;
}
}

void LtcDecoder::Feed( const float* samples, size_t count, int sampleRate, double time, int stride )
{
	if( sampleRate <= 0 || !samples )
		return;
	if( sampleRate != rate )
	{
		rate        = sampleRate;
		period      = rate / 2160.0; // between 24 fps (1920 bits a second) and 30 (2400)
		sinceEdge   = 0;
		halfPending = false;
		filled      = 0;
	}
	for( size_t i = 0; i < count; ++i )
	{
		float x  = samples[ i * stride ];
		envelope = std::max( std::fabs( x ), envelope * 0.9995f );
		float h  = std::max( 0.002f, envelope * 0.2f ); // hysteresis: noise around zero is no edge
		sinceEdge += 1;
		++sample;
		if( high ? x > -h : x < h )
			continue;
		high            = !high;
		double interval = sinceEdge;
		sinceEdge       = 0;
		double at       = time - (double)( count - 1 - i ) / rate;
		if( interval > period * 1.6 || interval < period * 0.3 )
		{
			halfPending = false; // silence, noise, or another signal: start again
			continue;
		}
		if( interval > period * 0.75 )
		{
			// A whole cell without a middle edge: a 0.
			if( halfPending )
				halfPending = false; // out of step: this edge starts a new cell
			else
				Bit( 0, at );
			period = period * 0.95 + interval * 0.05;
		}
		else
		{
			// Half a cell: two of them make a 1.
			if( halfPending )
			{
				Bit( 1, at );
				halfPending = false;
			}
			else
				halfPending = true;
			period = period * 0.95 + interval * 2 * 0.05;
		}
		period = std::clamp( period, rate / 2700.0, rate / 1700.0 );
	}
}

void LtcDecoder::Bit( int bit, double time )
{
	std::memmove( bits.data(), bits.data() + 1, bits.size() - 1 );
	bits[ 79 ] = (uint8_t)bit;
	filled     = std::min( filled + 1, 80 );
	if( filled < 80 || std::memcmp( bits.data() + 64, kSync, 16 ) != 0 )
		return;
	filled = 0;
	Timecode tc;
	tc.frames  = Field( bits, 0, 4 ) + 10 * Field( bits, 8, 2 );
	tc.seconds = Field( bits, 16, 4 ) + 10 * Field( bits, 24, 3 );
	tc.minutes = Field( bits, 32, 4 ) + 10 * Field( bits, 40, 3 );
	tc.hours   = Field( bits, 48, 4 ) + 10 * Field( bits, 56, 2 );
	bool drop  = bits[ 10 ] != 0;
	if( tc.frames > 29 || tc.seconds > 59 || tc.minutes > 59 || tc.hours > 23 )
		return;
	// The frame rate from how often words come (else from the bits' tempo); drop frame is flagged.
	if( lastWordAt >= 0 )
	{
		double between = time - lastWordAt;
		if( between > 0.03 && between < 0.045 )
			wordPeriod = wordPeriod > 0 ? wordPeriod * 0.8 + between * 0.2 : between;
	}
	lastWordAt = time;
	double fps = wordPeriod > 0 ? 1.0 / wordPeriod : rate / ( 80.0 * period );
	if( drop )
		tc.rate = TimecodeRate::Fps2997Drop;
	else
		tc.rate = fps < 24.5 ? TimecodeRate::Fps24 : fps < 27.5 ? TimecodeRate::Fps25 : TimecodeRate::Fps30;
	++framesDecoded;
	// A word ends as its frame does: now is the start of the next.
	receiver.Receive( tc, time - 1.0 / TimecodeRateFps( tc.rate ), true );
}

// MTC ------------------------------------------------------------------------------------------------------

void MtcDecoder::Message( const unsigned char* bytes, size_t size, double time )
{
	if( size >= 2 && bytes[ 0 ] == 0xF1 )
	{
		int piece = ( bytes[ 1 ] >> 4 ) & 7, value = bytes[ 1 ] & 0x0F;
		if( piece == 0 )
		{
			seen = 0;
			next = 0;
		}
		if( piece != next )
		{
			seen = 0; // out of order (or backwards): wait for the next piece 0
			next = 0;
			return;
		}
		pieces[ piece ] = value;
		seen |= 1 << piece;
		next = piece + 1;
		if( piece == 7 && seen == 0xFF )
		{
			Timecode tc;
			tc.frames  = pieces[ 0 ] | ( pieces[ 1 ] & 1 ) << 4;
			tc.seconds = pieces[ 2 ] | ( pieces[ 3 ] & 3 ) << 4;
			tc.minutes = pieces[ 4 ] | ( pieces[ 5 ] & 3 ) << 4;
			tc.hours   = pieces[ 6 ] | ( pieces[ 7 ] & 1 ) << 4;
			tc.rate    = RateFromBits( pieces[ 7 ] >> 1 );
			seen = next = 0;
			if( tc.frames > 29 || tc.seconds > 59 || tc.minutes > 59 || tc.hours > 23 )
				return;
			// The time is that of piece 0, sent a frame and three quarters ago.
			receiver.Receive( tc, time - 1.75 / TimecodeRateFps( tc.rate ), true );
		}
		return;
	}
	// Full frame: F0 7F <device> 01 01 hh mm ss ff F7, sent on a locate.
	if( size >= 10 && bytes[ 0 ] == 0xF0 && bytes[ 1 ] == 0x7F && bytes[ 3 ] == 0x01 && bytes[ 4 ] == 0x01 )
	{
		Timecode tc;
		tc.rate    = RateFromBits( bytes[ 5 ] >> 5 );
		tc.hours   = bytes[ 5 ] & 0x1F;
		tc.minutes = bytes[ 6 ] & 0x3F;
		tc.seconds = bytes[ 7 ] & 0x3F;
		tc.frames  = bytes[ 8 ] & 0x1F;
		if( tc.frames > 29 || tc.seconds > 59 || tc.minutes > 59 || tc.hours > 23 )
			return;
		receiver.Receive( tc, time, false );
	}
}

// Art-Net --------------------------------------------------------------------------------------------------

bool ParseArtTimecode( const uint8_t* data, size_t size, Timecode& timecode )
{
	// "Art-Net\0", OpCode 0x9700 (low byte first), protocol version, two fillers (the second Art-Net 4's stream),
	// frames, seconds, minutes, hours, type.
	if( size < 19 || std::memcmp( data, "Art-Net\0", 8 ) != 0 || data[ 8 ] != 0x00 || data[ 9 ] != 0x97 )
		return false;
	Timecode tc;
	tc.frames  = data[ 14 ];
	tc.seconds = data[ 15 ];
	tc.minutes = data[ 16 ];
	tc.hours   = data[ 17 ];
	tc.rate    = RateFromBits( data[ 18 ] );
	if( tc.frames > 29 || tc.seconds > 59 || tc.minutes > 59 || tc.hours > 23 )
		return false;
	timecode = tc;
	return true;
}
}
