#include "MovFile.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace stltvj
{
namespace
{
constexpr uint32_t Fourcc( const char ( &s )[ 5 ] )
{
	return ( (uint32_t)(uint8_t)s[ 0 ] << 24 ) | ( (uint32_t)(uint8_t)s[ 1 ] << 16 ) | ( (uint32_t)(uint8_t)s[ 2 ] << 8 ) | (uint32_t)(uint8_t)s[ 3 ];
}

uint16_t Be16( const uint8_t* p )
{
	return (uint16_t)( ( p[ 0 ] << 8 ) | p[ 1 ] );
}

uint32_t Be32( const uint8_t* p )
{
	return ( (uint32_t)p[ 0 ] << 24 ) | ( (uint32_t)p[ 1 ] << 16 ) | ( (uint32_t)p[ 2 ] << 8 ) | p[ 3 ];
}

uint64_t Be64( const uint8_t* p )
{
	return ( (uint64_t)Be32( p ) << 32 ) | Be32( p + 4 );
}

// A view over the payload of an atom with child atoms.
struct Span
{
	const uint8_t* data = nullptr;
	size_t size         = 0;
};

// Calls visit( type, payload ) for each child atom; false if the layout is broken.
template< typename Visit >
bool ForEachAtom( Span span, Visit visit )
{
	size_t at = 0;
	while( at + 8 <= span.size )
	{
		uint64_t size  = Be32( span.data + at );
		uint32_t type  = Be32( span.data + at + 4 );
		size_t header  = 8;
		if( size == 1 )
		{
			if( at + 16 > span.size )
				return false;
			size   = Be64( span.data + at + 8 );
			header = 16;
		}
		else if( size == 0 )
		{
			size = span.size - at;
		}
		if( size < header || size > span.size - at )
			return false;
		visit( type, Span{ span.data + at + header, (size_t)( size - header ) } );
		at += (size_t)size;
	}
	return true;
}

Span Child( Span parent, uint32_t wanted )
{
	Span found;
	ForEachAtom( parent, [ & ]( uint32_t type, Span payload ) {
		if( type == wanted && !found.data )
			found = payload;
	} );
	return found;
}

struct Tables
{
	std::vector< std::pair< uint32_t, uint32_t > > timeToSample; // count, delta
	std::vector< uint32_t > sampleSizes;
	uint32_t constantSize = 0;
	uint32_t sampleCount  = 0;
	struct ChunkRun
	{
		uint32_t firstChunk, samplesPerChunk;
	};
	std::vector< ChunkRun > sampleToChunk;
	std::vector< uint64_t > chunkOffsets;
};

bool ReadTables( Span stbl, Tables& t )
{
	Span stts = Child( stbl, Fourcc( "stts" ) );
	if( stts.size >= 8 )
	{
		uint32_t entries = Be32( stts.data + 4 );
		for( uint32_t i = 0; i < entries && 8 + i * 8 + 8 <= stts.size; ++i )
			t.timeToSample.push_back( { Be32( stts.data + 8 + i * 8 ), Be32( stts.data + 12 + i * 8 ) } );
	}
	Span stsz = Child( stbl, Fourcc( "stsz" ) );
	if( stsz.size < 12 )
		return false;
	t.constantSize = Be32( stsz.data + 4 );
	t.sampleCount  = Be32( stsz.data + 8 );
	if( t.constantSize == 0 )
	{
		if( stsz.size < 12 + (size_t)t.sampleCount * 4 )
			return false;
		t.sampleSizes.resize( t.sampleCount );
		for( uint32_t i = 0; i < t.sampleCount; ++i )
			t.sampleSizes[ i ] = Be32( stsz.data + 12 + i * 4 );
	}
	Span stsc = Child( stbl, Fourcc( "stsc" ) );
	if( stsc.size < 8 )
		return false;
	uint32_t runs = Be32( stsc.data + 4 );
	for( uint32_t i = 0; i < runs && 8 + i * 12 + 12 <= stsc.size; ++i )
		t.sampleToChunk.push_back( { Be32( stsc.data + 8 + i * 12 ), Be32( stsc.data + 12 + i * 12 ) } );

	Span stco = Child( stbl, Fourcc( "stco" ) );
	Span co64 = Child( stbl, Fourcc( "co64" ) );
	if( stco.size >= 8 )
	{
		uint32_t chunks = Be32( stco.data + 4 );
		for( uint32_t i = 0; i < chunks && 8 + i * 4 + 4 <= stco.size; ++i )
			t.chunkOffsets.push_back( Be32( stco.data + 8 + i * 4 ) );
	}
	else if( co64.size >= 8 )
	{
		uint32_t chunks = Be32( co64.data + 4 );
		for( uint32_t i = 0; i < chunks && 8 + i * 8 + 8 <= co64.size; ++i )
			t.chunkOffsets.push_back( Be64( co64.data + 8 + i * 8 ) );
	}
	return !t.chunkOffsets.empty() && !t.sampleToChunk.empty();
}
}

std::string FourccString( uint32_t fourcc )
{
	std::string s( 4, ' ' );
	for( int i = 0; i < 4; ++i )
	{
		char c   = (char)( ( fourcc >> ( 24 - 8 * i ) ) & 0xFF );
		s[ i ] = ( c >= 32 && c < 127 ) ? c : '?';
	}
	return s;
}

bool IsHapCodec( uint32_t fourcc )
{
	return HapCodecName( fourcc ) != nullptr;
}

const char* HapCodecName( uint32_t fourcc )
{
	switch( fourcc )
	{
	case Fourcc( "Hap1" ): return "HAP";
	case Fourcc( "Hap5" ): return "HAP Alpha";
	case Fourcc( "HapY" ): return "HAP Q";
	case Fourcc( "HapM" ): return "HAP Q Alpha";
	case Fourcc( "HapA" ): return "HAP Alpha-Only";
	case Fourcc( "Hap7" ): return "HAP R";
	case Fourcc( "HapH" ): return "HAP HDR";
	default: return nullptr;
	}
}

double VideoTrack::FrameRate() const
{
	double seconds = DurationSeconds();
	return seconds > 0 ? samples.size() / seconds : 0.0;
}

size_t VideoTrack::FrameAt( double seconds ) const
{
	if( samples.empty() )
		return 0;
	int64_t units = (int64_t)std::floor( seconds * timescale );
	auto after    = std::upper_bound( samples.begin(), samples.end(), units, []( int64_t t, const Sample& s ) { return t < s.time; } );
	if( after == samples.begin() )
		return 0;
	return std::min( (size_t)( after - samples.begin() ) - 1, samples.size() - 1 );
}

double VideoTrack::FrameTime( size_t frame ) const
{
	if( samples.empty() || !timescale )
		return 0.0;
	return (double)samples[ std::min( frame, samples.size() - 1 ) ].time / timescale;
}

bool MovFile::Fail( const std::string& message )
{
	error = message;
	reader.Close();
	return false;
}

bool MovFile::Open( const std::string& path )
{
	video = VideoTrack{};
	error.clear();
	if( !reader.Open( path ) )
		return Fail( "Cannot open the file" );

	// Walk the top-level atoms from the file itself: the movie header may sit after gigabytes of frames.
	uint64_t at = 0;
	std::vector< uint8_t > moov;
	while( at + 8 <= reader.Size() )
	{
		uint8_t header[ 16 ];
		if( !reader.ReadAt( at, header, 8 ) )
			return Fail( "Read error" );
		uint64_t size     = Be32( header );
		uint32_t type     = Be32( header + 4 );
		uint64_t headSize = 8;
		if( size == 1 )
		{
			if( !reader.ReadAt( at + 8, header + 8, 8 ) )
				return Fail( "Read error" );
			size     = Be64( header + 8 );
			headSize = 16;
		}
		else if( size == 0 )
		{
			size = reader.Size() - at;
		}
		if( size < headSize || size > reader.Size() - at )
			break;
		if( type == Fourcc( "moov" ) )
		{
			if( size - headSize > ( 512ull << 20 ) )
				return Fail( "Movie header too large" );
			moov.resize( (size_t)( size - headSize ) );
			if( !reader.ReadAt( at + headSize, moov.data(), moov.size() ) )
				return Fail( "Read error" );
			break;
		}
		at += size;
	}
	if( moov.empty() )
		return Fail( "Not a QuickTime or MP4 movie (no moov atom)" );
	if( !ParseMovie( moov ) )
		return false;
	return true;
}

bool MovFile::ParseMovie( const std::vector< uint8_t >& moovData )
{
	Span moov{ moovData.data(), moovData.size() };
	bool found = false;
	ForEachAtom( moov, [ & ]( uint32_t type, Span trak ) {
		if( found || type != Fourcc( "trak" ) )
			return;
		Span mdia = Child( trak, Fourcc( "mdia" ) );
		Span hdlr = Child( mdia, Fourcc( "hdlr" ) );
		if( hdlr.size < 12 || Be32( hdlr.data + 8 ) != Fourcc( "vide" ) )
			return;
		Span mdhd = Child( mdia, Fourcc( "mdhd" ) );
		Span stbl = Child( Child( mdia, Fourcc( "minf" ) ), Fourcc( "stbl" ) );
		Span stsd = Child( stbl, Fourcc( "stsd" ) );
		if( mdhd.size < 24 || stsd.size < 8 + 36 )
			return;

		VideoTrack track;
		if( mdhd.data[ 0 ] == 1 )
		{
			if( mdhd.size < 36 )
				return;
			track.timescale = Be32( mdhd.data + 20 );
			track.duration  = (int64_t)Be64( mdhd.data + 24 );
		}
		else
		{
			track.timescale = Be32( mdhd.data + 12 );
			track.duration  = Be32( mdhd.data + 16 );
		}
		// First sample description: size, format, 6 reserved, data ref index, then the visual sample entry.
		const uint8_t* entry = stsd.data + 8;
		track.codec          = Be32( entry + 4 );
		track.width          = Be16( entry + 32 );
		track.height         = Be16( entry + 34 );

		Tables tables;
		if( !ReadTables( stbl, tables ) )
			return;

		uint32_t count = tables.sampleCount;
		track.samples.resize( count );
		// Sizes and times.
		int64_t time  = 0;
		size_t sample = 0;
		for( auto [ runCount, delta ] : tables.timeToSample )
			for( uint32_t i = 0; i < runCount && sample < count; ++i, ++sample )
			{
				track.samples[ sample ].time = time;
				time += delta;
			}
		for( ; sample < count; ++sample )
			track.samples[ sample ].time = time;
		for( uint32_t i = 0; i < count; ++i )
			track.samples[ i ].size = tables.constantSize ? tables.constantSize : tables.sampleSizes[ i ];

		// Offsets: samples are laid out back to back inside each chunk.
		sample = 0;
		for( size_t run = 0; run < tables.sampleToChunk.size() && sample < count; ++run )
		{
			uint32_t first = tables.sampleToChunk[ run ].firstChunk;
			uint32_t last  = run + 1 < tables.sampleToChunk.size() ? tables.sampleToChunk[ run + 1 ].firstChunk : (uint32_t)tables.chunkOffsets.size() + 1;
			for( uint32_t chunk = first; chunk < last && chunk >= 1 && chunk <= tables.chunkOffsets.size() && sample < count; ++chunk )
			{
				uint64_t offset = tables.chunkOffsets[ chunk - 1 ];
				for( uint32_t i = 0; i < tables.sampleToChunk[ run ].samplesPerChunk && sample < count; ++i, ++sample )
				{
					track.samples[ sample ].offset = offset;
					offset += track.samples[ sample ].size;
				}
			}
		}
		if( sample < count )
			track.samples.resize( sample );
		if( track.duration <= 0 || time > track.duration )
			track.duration = time;
		video = std::move( track );
		found = true;
	} );
	if( !found )
		return Fail( "No video track" );
	if( video.samples.empty() )
		return Fail( "The video track has no frames" );
	for( const Sample& s : video.samples )
		if( s.offset + s.size > reader.Size() )
			return Fail( "The file is truncated" );
	return true;
}

bool MovFile::ReadSample( size_t frame, std::vector< uint8_t >& buffer ) const
{
	if( frame >= video.samples.size() )
		return false;
	const Sample& s = video.samples[ frame ];
	if( buffer.size() < s.size )
		buffer.resize( s.size );
	return reader.ReadAt( s.offset, buffer.data(), s.size );
}
}
