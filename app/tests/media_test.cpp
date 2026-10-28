// Tests for the media engine: QuickTime demuxing, frame timing and HAP decoding.
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>
#include <hap.h>
#include "media/HapFrame.h"
#include "media/MovFile.h"
#include "media/ThreadPool.h"

using namespace stltvj;

static int failures = 0;

#define CHECK( condition )                                                         \
	do                                                                             \
	{                                                                              \
		if( !( condition ) )                                                       \
		{                                                                          \
			std::fprintf( stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition ); \
			++failures;                                                            \
		}                                                                          \
	} while( 0 )

static std::string Fixture( const char* name )
{
	return std::string( FIXTURES_DIR ) + "/" + name;
}

// Opens a movie and decodes every frame; returns the layout of the last one.
static HapLayout DecodeAll( MovFile& movie, ThreadPool* pool, std::vector< uint8_t >& decoded )
{
	std::vector< uint8_t > compressed;
	HapLayout layout;
	const VideoTrack& video = movie.Video();
	for( size_t f = 0; f < video.samples.size(); ++f )
	{
		CHECK( movie.ReadSample( f, compressed ) );
		CHECK( ProbeHapFrame( compressed.data(), video.samples[ f ].size, video.width, video.height, layout ) );
		decoded.assign( layout.TotalBytes(), 0 );
		void* outputs[ 2 ] = { decoded.data(), decoded.data() + layout.bytes[ 0 ] };
		CHECK( DecodeHapFrame( compressed.data(), video.samples[ f ].size, layout, outputs, pool ) );
	}
	return layout;
}

static void TestHapDxt1()
{
	MovFile movie;
	CHECK( movie.Open( Fixture( "green_hap.mov" ) ) );
	const VideoTrack& video = movie.Video();
	CHECK( FourccString( video.codec ) == "Hap1" );
	CHECK( std::string( HapCodecName( video.codec ) ) == "HAP" );
	CHECK( video.width == 64 && video.height == 48 );
	CHECK( video.samples.size() == 10 );
	CHECK( std::fabs( video.FrameRate() - 25.0 ) < 0.01 );

	std::vector< uint8_t > decoded;
	HapLayout layout = DecodeAll( movie, nullptr, decoded );
	CHECK( layout.textureCount == 1 );
	CHECK( layout.formats[ 0 ] == HapTextureFormat_RGB_DXT1 );
	CHECK( layout.bytes[ 0 ] == ( 64 / 4 ) * ( 48 / 4 ) * 8 );
	// A pure green frame: the first block's endpoint colour is RGB565 green.
	uint16_t color0 = (uint16_t)( decoded[ 0 ] | ( decoded[ 1 ] << 8 ) );
	CHECK( ( color0 >> 11 ) == 0 );
	CHECK( ( ( color0 >> 5 ) & 63 ) >= 60 );
	CHECK( ( color0 & 31 ) == 0 );
}

static void TestHapAlpha()
{
	MovFile movie;
	CHECK( movie.Open( Fixture( "alpha_hap.mov" ) ) );
	CHECK( FourccString( movie.Video().codec ) == "Hap5" );
	CHECK( movie.Video().samples.size() == 6 );
	std::vector< uint8_t > decoded;
	HapLayout layout = DecodeAll( movie, nullptr, decoded );
	CHECK( layout.formats[ 0 ] == HapTextureFormat_RGBA_DXT5 );
	// DXT5 alpha endpoints of a half-transparent frame sit near 128.
	CHECK( std::abs( (int)decoded[ 0 ] - 128 ) <= 2 );
}

static void TestChunkedHapQ()
{
	MovFile movie;
	CHECK( movie.Open( Fixture( "chunked_hapq.mov" ) ) );
	const VideoTrack& video = movie.Video();
	CHECK( FourccString( video.codec ) == "HapY" );
	CHECK( video.samples.size() == 24 );
	CHECK( std::fabs( video.DurationSeconds() - 1.0 ) < 1e-6 );

	// Frame timing: 24 fps, so 0.5 s is frame 12 and the end clamps to the last frame.
	CHECK( video.FrameAt( 0.0 ) == 0 );
	CHECK( video.FrameAt( 0.5 ) == 12 );
	CHECK( video.FrameAt( 0.5 - 1e-4 ) == 11 );
	CHECK( video.FrameAt( 10.0 ) == 23 );
	CHECK( video.FrameAt( -1.0 ) == 0 );
	CHECK( std::fabs( video.FrameTime( 12 ) - 0.5 ) < 1e-9 );

	// The same frames decoded on one thread and on the pool must match byte for byte.
	ThreadPool pool( 3 );
	std::vector< uint8_t > serial, parallel;
	HapLayout layout = DecodeAll( movie, nullptr, serial );
	DecodeAll( movie, &pool, parallel );
	CHECK( layout.formats[ 0 ] == HapTextureFormat_YCoCg_DXT5 );
	CHECK( layout.chunks[ 0 ] == 4 );
	CHECK( serial == parallel );
}

static void TestThreadPool()
{
	ThreadPool pool( 4 );
	// Many callers sharing the workers at once, like several layers decoding together.
	std::vector< std::thread > callers;
	std::atomic< int > total{ 0 };
	for( int c = 0; c < 6; ++c )
		callers.emplace_back( [ & ] {
			for( int round = 0; round < 200; ++round )
			{
				std::vector< int > hits( 7, 0 );
				pool.Run( 7, [ & ]( unsigned i ) { hits[ i ]++; } );
				for( int h : hits )
					if( h == 1 )
						total++;
			}
		} );
	for( std::thread& t : callers )
		t.join();
	CHECK( total.load() == 6 * 200 * 7 );
}

static void TestBadFiles()
{
	MovFile movie;
	CHECK( !movie.Open( Fixture( "does_not_exist.mov" ) ) );
	CHECK( !movie.Error().empty() );
	CHECK( !movie.Open( std::string( FIXTURES_DIR ) + "/../media_test.cpp" ) );

	HapLayout layout;
	uint8_t junk[ 64 ] = { 1, 2, 3, 4, 5 };
	CHECK( !ProbeHapFrame( junk, sizeof( junk ), 64, 48, layout ) );
}

int main()
{
	TestHapDxt1();
	TestHapAlpha();
	TestChunkedHapQ();
	TestThreadPool();
	TestBadFiles();
	if( failures )
	{
		std::fprintf( stderr, "%d check(s) failed\n", failures );
		return 1;
	}
	std::printf( "media tests passed\n" );
	return 0;
}
