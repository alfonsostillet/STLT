#include "nimslink/Protocol.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

namespace nimslink
{
namespace
{
const uint8_t kMagic[ 4 ] = { 'N', 'I', 'M', 'S' };

struct Writer
{
	std::vector< uint8_t > bytes;
	void U8( uint8_t v ) { bytes.push_back( v ); }
	void U16( uint16_t v )
	{
		for( int i = 0; i < 2; ++i )
			bytes.push_back( (uint8_t)( v >> ( 8 * i ) ) );
	}
	void U32( uint32_t v )
	{
		for( int i = 0; i < 4; ++i )
			bytes.push_back( (uint8_t)( v >> ( 8 * i ) ) );
	}
	void U64( uint64_t v )
	{
		for( int i = 0; i < 8; ++i )
			bytes.push_back( (uint8_t)( v >> ( 8 * i ) ) );
	}
	void F32( float v )
	{
		uint32_t u;
		std::memcpy( &u, &v, 4 );
		U32( u );
	}
	void F64( double v )
	{
		uint64_t u;
		std::memcpy( &u, &v, 8 );
		U64( u );
	}
	void Str( const std::string& s )
	{
		// At most 255 bytes, not cutting a UTF-8 character in two.
		size_t n = std::min< size_t >( s.size(), 255 );
		while( n > 0 && n < s.size() && ( (uint8_t)s[ n ] & 0xC0 ) == 0x80 )
			--n;
		U8( (uint8_t)n );
		bytes.insert( bytes.end(), s.begin(), s.begin() + (ptrdiff_t)n );
	}
};

struct Reader
{
	const uint8_t* data;
	size_t size, at = 0;
	bool ok = true;
	bool Need( size_t n )
	{
		if( !ok || size - at < n )
			ok = false;
		return ok;
	}
	uint8_t U8() { return Need( 1 ) ? data[ at++ ] : 0; }
	uint16_t U16()
	{
		if( !Need( 2 ) )
			return 0;
		uint16_t v = (uint16_t)( data[ at ] | ( data[ at + 1 ] << 8 ) );
		at += 2;
		return v;
	}
	uint32_t U32()
	{
		if( !Need( 4 ) )
			return 0;
		uint32_t v = 0;
		for( int i = 0; i < 4; ++i )
			v |= (uint32_t)data[ at + i ] << ( 8 * i );
		at += 4;
		return v;
	}
	uint64_t U64()
	{
		if( !Need( 8 ) )
			return 0;
		uint64_t v = 0;
		for( int i = 0; i < 8; ++i )
			v |= (uint64_t)data[ at + i ] << ( 8 * i );
		at += 8;
		return v;
	}
	float F32()
	{
		uint32_t u = U32();
		float v;
		std::memcpy( &v, &u, 4 );
		return std::isfinite( v ) ? v : 0.0f;
	}
	double F64()
	{
		uint64_t u = U64();
		double v;
		std::memcpy( &v, &u, 8 );
		return std::isfinite( v ) ? v : 0.0;
	}
	std::string Str()
	{
		size_t n = U8();
		if( !Need( n ) )
			return {};
		std::string s( (const char*)data + at, n );
		at += n;
		return s;
	}
};

std::string Folded( const std::string& s )
{
	// Lower case, runs of spaces and punctuation as one space, none at the ends: "Kerala " = "kerala".
	std::string out;
	bool space = false;
	for( unsigned char c : s )
	{
		if( c < 0x80 && !std::isalnum( c ) )
		{
			space = !out.empty();
			continue;
		}
		if( space )
			out += ' ';
		space = false;
		out += c < 0x80 ? (char)std::tolower( c ) : (char)c;
	}
	return out;
}
}

TrackId IdOf( const TrackInfo& info )
{
	TrackId id;
	id.title  = info.title;
	id.artist = info.artist;
	id.length = info.length;
	if( id.title.empty() && !info.file.empty() )
	{
		// No title in the tags: the file's name, without folder and extension.
		std::string f = info.file;
		size_t slash  = f.find_last_of( "/\\" );
		if( slash != std::string::npos )
			f = f.substr( slash + 1 );
		size_t dot = f.find_last_of( '.' );
		if( dot != std::string::npos && dot > 0 )
			f = f.substr( 0, dot );
		id.title = f;
	}
	return id;
}

bool SameTrack( const TrackId& a, const TrackId& b )
{
	if( a.Empty() || b.Empty() )
		return false;
	if( Folded( a.title ) != Folded( b.title ) || Folded( a.artist ) != Folded( b.artist ) )
		return false;
	return a.length <= 0 || b.length <= 0 || std::fabs( a.length - b.length ) < 1.0;
}

std::vector< uint8_t > Encode( const Packet& p )
{
	Writer w;
	w.bytes.insert( w.bytes.end(), kMagic, kMagic + 4 );
	w.U8( kVersion );
	w.U8( (uint8_t)p.type );
	w.U16( 0 );
	w.U32( p.from );
	switch( p.type )
	{
	case PacketType::Announce:
		w.U16( p.port );
		w.Str( p.name );
		w.Str( p.program );
		break;
	case PacketType::Subscribe:
		w.F32( p.roundTrip );
		w.Str( p.name );
		for( uint8_t v : p.visuals )
			w.U8( v );
		break;
	case PacketType::Ping: w.F64( p.t0 ); break;
	case PacketType::Pong:
		w.F64( p.t0 );
		w.F64( p.t1 );
		w.F64( p.t2 );
		break;
	case PacketType::Decks:
		w.U32( p.seq );
		w.F64( p.time );
		w.U8( kDecks );
		for( const DeckState& d : p.decks )
		{
			w.U8( (uint8_t)( ( d.loaded ? 1 : 0 ) | ( d.playing ? 2 : 0 ) | ( d.inLoop ? 4 : 0 ) ) );
			w.U16( d.track );
			w.F64( d.position );
			w.F32( d.rate );
			w.F32( d.beatPhase );
			w.F64( d.beat );
			w.F32( d.bpm );
			w.F64( d.loopStart );
			w.F64( d.loopLength );
			for( float s : d.stems )
				w.F32( s );
			w.F32( d.level );
			w.F32( d.volume );
		}
		break;
	case PacketType::Track:
	{
		const TrackInfo& t = p.track;
		w.U8( (uint8_t)p.deck );
		w.U16( t.version );
		w.Str( t.title );
		w.Str( t.artist );
		w.Str( t.key );
		w.Str( t.file );
		w.U64( (uint64_t)t.entry );
		w.F64( t.length );
		w.F32( t.bpm );
		w.U8( t.stems ? 1 : 0 );
		for( const Hotcue& h : t.hotcues )
		{
			w.U8( h.exists ? 1 : 0 );
			if( !h.exists )
				continue;
			w.U8( (uint8_t)(int8_t)h.type );
			w.F64( h.start );
			w.F64( h.length );
			w.Str( h.name );
		}
		break;
	}
	case PacketType::Bye: break;
	}
	return w.bytes;
}

bool Decode( const uint8_t* data, size_t size, Packet& p )
{
	if( size < 12 || std::memcmp( data, kMagic, 4 ) != 0 || data[ 4 ] != kVersion )
		return false;
	Reader r{ data, size, 5 };
	p      = Packet();
	p.type = (PacketType)r.U8();
	r.U16();
	p.from = r.U32();
	switch( p.type )
	{
	case PacketType::Announce:
		p.port    = r.U16();
		p.name    = r.Str();
		p.program = r.Str();
		break;
	case PacketType::Subscribe:
		p.roundTrip = r.F32();
		p.name      = r.Str();
		for( uint8_t& v : p.visuals )
			v = r.U8();
		break;
	case PacketType::Ping: p.t0 = r.F64(); break;
	case PacketType::Pong:
		p.t0 = r.F64();
		p.t1 = r.F64();
		p.t2 = r.F64();
		break;
	case PacketType::Decks:
	{
		p.seq  = r.U32();
		p.time = r.F64();
		int n  = r.U8();
		for( int i = 0; i < n && r.ok; ++i )
		{
			DeckState d;
			uint8_t flags = r.U8();
			d.loaded      = flags & 1;
			d.playing     = flags & 2;
			d.inLoop      = flags & 4;
			d.track       = r.U16();
			d.position    = r.F64();
			d.rate        = r.F32();
			d.beatPhase   = r.F32();
			d.beat        = r.F64();
			d.bpm         = r.F32();
			d.loopStart   = r.F64();
			d.loopLength  = r.F64();
			for( float& s : d.stems )
				s = r.F32();
			d.level  = r.F32();
			d.volume = r.F32();
			if( i < kDecks )
				p.decks[ i ] = d;
		}
		break;
	}
	case PacketType::Track:
	{
		TrackInfo& t = p.track;
		p.deck       = r.U8();
		t.version    = r.U16();
		t.title      = r.Str();
		t.artist     = r.Str();
		t.key        = r.Str();
		t.file       = r.Str();
		t.entry      = (int64_t)r.U64();
		t.length     = r.F64();
		t.bpm        = r.F32();
		t.stems      = r.U8() != 0;
		for( Hotcue& h : t.hotcues )
		{
			h.exists = r.U8() != 0;
			if( !h.exists )
				continue;
			h.type   = (int8_t)r.U8();
			h.start  = r.F64();
			h.length = r.F64();
			h.name   = r.Str();
		}
		if( p.deck >= kDecks )
			return false;
		break;
	}
	case PacketType::Bye: break;
	default: return false;
	}
	return r.ok;
}

// ---- ClockSync

void ClockSync::Reset()
{
	*this = ClockSync();
}

void ClockSync::Add( double t0, double t1, double t2, double t3 )
{
	double delay = ( t3 - t0 ) - ( t2 - t1 );
	if( !( delay >= 0 ) || delay > 1.0 )
		return; // a clock moved, or an answer to a ping long gone
	samples.push_back( { t3, ( ( t1 - t0 ) + ( t2 - t3 ) ) / 2, delay } );
	// The last 20 seconds (and no more than 200 round trips).
	while( samples.size() > 200 || ( samples.size() > 1 && t3 - samples.front().local > 20 ) )
		samples.pop_front();
	double best = samples.front().delay;
	for( const Sample& s : samples )
		best = std::min( best, s.delay );
	roundTrip = best;
	// The good ones: within 0.3 ms (or half the best round trip) of the best. A line through them (least squares)
	// when they span a few seconds; their mean otherwise.
	double keep = best + std::max( 0.0003, best * 0.5 );
	double n = 0, sx = 0, sy = 0, sxx = 0, sxy = 0, first = 0, lastLocal = 0;
	for( const Sample& s : samples )
		if( s.delay <= keep )
		{
			if( n == 0 )
				first = s.local;
			lastLocal = s.local;
			double x  = s.local - t3;
			n += 1;
			sx += x;
			sy += s.offset;
			sxx += x * x;
			sxy += x * s.offset;
		}
	reference = t3;
	drift     = 0;
	offset    = sy / n;
	if( n >= 4 && lastLocal - first > 3 )
	{
		double d = n * sxx - sx * sx;
		if( d > 0 )
		{
			double b = ( n * sxy - sx * sy ) / d;
			if( std::fabs( b ) < 0.0005 ) // clocks differ by a few parts per million; more is noise
			{
				drift  = b;
				offset = ( sy - b * sx ) / n;
			}
		}
	}
	ready = true;
}

double ClockSync::Remote( double local ) const
{
	return local + offset + drift * ( local - reference );
}

double ClockSync::Local( double remote ) const
{
	// remote = local (1 + drift) + offset - drift x reference
	return ( remote - offset + drift * reference ) / ( 1 + drift );
}

// ---- DeckTracker

void DeckTracker::Reset()
{
	*this = DeckTracker();
}

double DeckTracker::Line( double local ) const
{
	return linePosition + lineRate * ( local - lineTime );
}

void DeckTracker::Add( const DeckState& s, double local )
{
	bool wasPlaying = any && state.loaded && state.playing;
	bool playing    = s.loaded && s.playing;
	bool sameTrack  = any && state.loaded && s.loaded && state.track == s.track;
	double rate     = s.rate > 0 ? s.rate : 1.0;
	// Where the deck should be, from what came before.
	double expected = !sameTrack ? s.position : wasPlaying ? Line( local ) : state.position;
	bool jumped     = sameTrack && std::fabs( s.position - expected ) > kJumpThreshold;
	if( jumped )
	{
		++jumps;
		jumpFrom    = expected;
		jumpTo      = s.position;
		jumpHotcue  = 0;
		jumpWasLoop = false;
		const TrackInfo* t = hasTrack && track.version == s.track ? &track : nullptr;
		double nearest     = 0.06;
		for( int h = 0; t && h < kHotcues; ++h )
			if( t->hotcues[ h ].exists && std::fabs( t->hotcues[ h ].start - s.position ) < nearest )
			{
				nearest    = std::fabs( t->hotcues[ h ].start - s.position );
				jumpHotcue = h + 1;
			}
		jumpWasLoop = s.inLoop && !jumpHotcue && std::fabs( s.loopStart - s.position ) < 0.06;
	}
	if( playing && !wasPlaying )
		++starts;
	if( playing )
	{
		// A new line after a jump, a new track, a start or a change of speed; otherwise the sample joins it.
		if( jumped || !wasPlaying || !sameTrack || std::fabs( rate - lineRate ) > 0.0005 )
			window.clear();
		window.emplace_back( local, s.position - rate * local );
		while( window.size() > 1 && local - window.front().first > 1.0 )
			window.pop_front();
		double best = window.front().second;
		for( const auto& w : window )
			best = std::max( best, w.second ); // the least late sample is the one furthest ahead
		lineRate     = rate;
		lineTime     = local;
		linePosition = best + rate * local;
	}
	else
		window.clear();
	double trackBpm = hasTrack && track.version == s.track && track.bpm > 0 ? track.bpm : rate > 0 ? s.bpm / rate : s.bpm;
	beatOffset      = s.beat - s.position * trackBpm / 60.0;
	state           = s;
	last            = local;
	any             = true;
}

DeckNow DeckTracker::At( double now ) const
{
	DeckNow d;
	if( !any )
		return d;
	d.present     = true;
	d.state       = state;
	d.age         = now - last;
	bool playing  = state.loaded && state.playing;
	// While it plays, the line; after two seconds without news it stops there (the transmitter went away).
	double until  = std::min( now, last + 2.0 );
	d.position    = playing && !window.empty() ? Line( until ) : state.position;
	double rate   = state.rate > 0 ? state.rate : 1.0;
	double trackBpm = hasTrack && track.version == state.track && track.bpm > 0 ? track.bpm : state.bpm / rate;
	d.bpm         = state.bpm;
	d.beat        = trackBpm > 0 ? d.position * trackBpm / 60.0 + beatOffset : state.beat;
	d.jumps       = jumps;
	d.jumpFrom    = jumpFrom;
	d.jumpTo      = jumpTo;
	d.jumpHotcue  = jumpHotcue;
	d.jumpWasLoop = jumpWasLoop;
	d.starts      = starts;
	return d;
}

int LeadingDeck( const DeckNow* decks, int count, int current )
{
	auto on = [ & ]( int i ) { return i >= 0 && i < count && decks[ i ].present && decks[ i ].state.loaded && decks[ i ].state.playing; };
	int best = -1;
	for( int i = 0; i < count; ++i )
		if( on( i ) && ( best < 0 || decks[ i ].state.volume > decks[ best ].state.volume ) )
			best = i;
	if( best >= 0 && on( current ) && decks[ best ].state.volume < decks[ current ].state.volume + 0.1f )
		return current;
	return best;
}
}
