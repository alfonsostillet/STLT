// Tests for the media engine: QuickTime demuxing, frame timing and HAP decoding.
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>
#include <hap.h>
#include "../tools/BcDecode.h"
#include "media/DxvFrame.h"
#include "media/FrameSource.h"
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

static void TestFrameSourceHap()
{
	std::string error;
	std::unique_ptr< FrameSource > source = OpenFrameSource( Fixture( "chunked_hapq.mov" ), error );
	CHECK( source != nullptr );
	if( !source )
		return;
	CHECK( source->IsHap() );
	CHECK( source->CodecName() == "HAP Q" );
	const FrameLayout& layout = source->Layout();
	CHECK( layout.planeCount == 1 && layout.planes[ 0 ].format == PlaneFormat::Compressed );
	CHECK( layout.shading == Shading::YCoCg );
	std::vector< uint8_t > buffer( layout.totalBytes );
	CHECK( source->Decode( 23, buffer.data() ) );
	CHECK( source->Decode( 0, buffer.data() ) );
	CHECK( !source->Decode( 24, buffer.data() ) );
}

static void TestFrameSourceFfmpeg()
{
	std::string error;
	std::unique_ptr< FrameSource > missing = OpenFrameSource( Fixture( "nope.mp4" ), error );
	CHECK( !missing && !error.empty() );
	if( !HasFfmpeg() )
	{
		std::printf( "built without FFmpeg: non-HAP tests skipped\n" );
		return;
	}

	// H.264, 12 frames at 10 fps, keyframe every 4; frame n is a flat grey with luma 16 + 16 n.
	std::unique_ptr< FrameSource > movie = OpenFrameSource( Fixture( "ramp_h264.mp4" ), error );
	CHECK( movie != nullptr );
	if( movie )
	{
		CHECK( !movie->IsHap() && !movie->IsStill() );
		CHECK( movie->CodecName() == "H.264" );
		CHECK( movie->FrameCount() == 12 );
		CHECK( std::fabs( movie->FrameRate() - 10 ) < 1e-6 );
		CHECK( std::fabs( movie->Duration() - 1.2 ) < 0.05 );
		CHECK( movie->FrameAt( 0.55 ) == 5 );
		const FrameLayout& layout = movie->Layout();
		CHECK( layout.shading == Shading::Yuv && layout.planeCount == 3 );
		CHECK( layout.planes[ 0 ].width == 64 && layout.planes[ 0 ].height == 48 );
		CHECK( layout.planes[ 1 ].width == 32 && layout.planes[ 1 ].height == 24 );
		CHECK( !layout.fullRange );
		std::vector< uint8_t > buffer( layout.totalBytes );
		// In order, then jumping around: backwards, across keyframes, to the end and to the start.
		for( int frame : { 0, 1, 2, 3, 9, 3, 11, 5, 6, 0, 10 } )
		{
			bool ok = movie->Decode( (size_t)frame, buffer.data() );
			CHECK( ok );
			int expected = 16 + 16 * frame;
			if( ok && std::abs( buffer[ 0 ] - expected ) > 1 )
			{
				std::fprintf( stderr, "frame %d: luma %d, expected %d\n", frame, buffer[ 0 ], expected );
				++failures;
			}
			CHECK( std::abs( buffer[ layout.planes[ 1 ].offset ] - 128 ) <= 1 );
		}
	}

	// Scratching (ramp_h264.mp4: keyframes at 0, 4 and 8): frames kept from before, the keyframe after a seek while
	// the playhead moves, giving up on frames nobody wants.
	{
		auto luma = []( FrameSource& source, size_t frame, const DecodeRequest& request, int64_t& decoded, DecodeResult& result ) {
			std::vector< uint8_t > pixels( source.Layout().totalBytes );
			result = source.DecodeFrame( frame, pixels.data(), request, decoded );
			return (int)pixels[ 0 ];
		};
		DecodeRequest plain;
		DecodeRequest moving;
		moving.approximate = [] { return true; };
		DecodeRequest leftBehind = moving;
		leftBehind.abandon       = [] { return true; };
		int64_t decoded = -1;
		DecodeResult result;

		std::unique_ptr< FrameSource > a = OpenFrameSource( Fixture( "ramp_h264.mp4" ), error );
		CHECK( a != nullptr );
		if( a )
		{
			int y = luma( *a, 7, plain, decoded, result ); // decodes on from the first frame, keeping 1..6
			CHECK( result == DecodeResult::Exact && decoded == 7 && std::abs( y - ( 16 + 16 * 7 ) ) <= 1 );
			y = luma( *a, 5, plain, decoded, result ); // kept: no decoding
			CHECK( result == DecodeResult::Exact && decoded == 5 && std::abs( y - ( 16 + 16 * 5 ) ) <= 1 );
		}
		std::unique_ptr< FrameSource > c = OpenFrameSource( Fixture( "ramp_h264.mp4" ), error );
		CHECK( c != nullptr );
		if( c )
		{
			luma( *c, 1, plain, decoded, result );           // the decoder at frame 1
			int y = luma( *c, 10, moving, decoded, result ); // behind keyframe 8: the keyframe first, while moving
			CHECK( result == DecodeResult::Approximate && decoded == 8 && std::abs( y - ( 16 + 16 * 8 ) ) <= 1 );
			y = luma( *c, 10, plain, decoded, result ); // then on from it
			CHECK( result == DecodeResult::Exact && decoded == 10 && std::abs( y - ( 16 + 16 * 10 ) ) <= 1 );
		}
		std::unique_ptr< FrameSource > b = OpenFrameSource( Fixture( "ramp_h264.mp4" ), error );
		CHECK( b != nullptr );
		if( b )
		{
			DecodeRequest gone;
			gone.abandon = [] { return true; };
			luma( *b, 3, gone, decoded, result ); // nobody wants it: nothing written
			CHECK( result == DecodeResult::Abandoned );
			leftBehind.shown = 0;
			int y = luma( *b, 3, leftBehind, decoded, result ); // left behind while scratching: as far as it got
			CHECK( result == DecodeResult::Approximate && decoded > 0 && decoded < 3 && std::abs( y - ( 16 + 16 * (int)decoded ) ) <= 1 );
			SetFrameCacheMegabytes( 0 ); // nothing kept: still right
			y = luma( *b, 3, plain, decoded, result );
			CHECK( result == DecodeResult::Exact && decoded == 3 && std::abs( y - ( 16 + 16 * 3 ) ) <= 1 );
			y = luma( *b, 1, plain, decoded, result );
			CHECK( result == DecodeResult::Exact && std::abs( y - ( 16 + 16 * 1 ) ) <= 1 );
			SetFrameCacheMegabytes( 256 );
			// A stopped clip keeps nothing; it still decodes after.
			b->ReleaseMemory();
			y = luma( *b, 1, plain, decoded, result );
			CHECK( result == DecodeResult::Exact && std::abs( y - ( 16 + 16 * 1 ) ) <= 1 );
		}
	}

	// NV12 (a Y plane and interleaved UV, what hardware decoders hand back) goes up as two textures, unconverted.
	std::unique_ptr< FrameSource > nv12 = OpenFrameSource( Fixture( "nv12_raw.mkv" ), error );
	CHECK( nv12 != nullptr );
	if( nv12 )
	{
		const FrameLayout& layout = nv12->Layout();
		CHECK( layout.shading == Shading::Nv12 && layout.planeCount == 2 );
		CHECK( layout.planes[ 0 ].format == PlaneFormat::R8 && layout.planes[ 0 ].width == 16 && layout.planes[ 0 ].height == 8 );
		CHECK( layout.planes[ 1 ].format == PlaneFormat::RG8 && layout.planes[ 1 ].width == 8 && layout.planes[ 1 ].height == 4 );
		CHECK( layout.planes[ 1 ].bytes == 8 * 4 * 2 );
		std::vector< uint8_t > pixels( layout.totalBytes );
		CHECK( nv12->Decode( 1, pixels.data() ) );
		const uint8_t* uv = pixels.data() + layout.planes[ 1 ].offset;
		CHECK( pixels[ 0 ] == 100 && uv[ 0 ] == 100 && uv[ 1 ] == 180 && uv[ 2 ] == 100 );
		CHECK( !nv12->IsHardwareDecoded() ); // raw video never goes to the hardware
	}

	// Hardware decoding falls back to the CPU where there is no video hardware (as on the test machines).
	SetHardwareDecoding( true );
	std::unique_ptr< FrameSource > hw = OpenFrameSource( Fixture( "ramp_h264.mp4" ), error );
	CHECK( hw != nullptr );
	if( hw )
	{
		std::vector< uint8_t > pixels( hw->Layout().totalBytes );
		CHECK( hw->Decode( 3, pixels.data() ) && std::abs( pixels[ 0 ] - 64 ) <= 1 );
		std::printf( "H.264 %s\n", hw->Details().c_str() );
	}

	std::unique_ptr< FrameSource > png = OpenFrameSource( Fixture( "still_rgba.png" ), error );
	CHECK( png != nullptr );
	if( png )
	{
		CHECK( png->IsStill() && png->FrameCount() == 1 && png->Duration() == 0 );
		CHECK( png->Layout().shading == Shading::Rgba && png->Layout().width == 8 && png->Layout().height == 4 );
		std::vector< uint8_t > pixels( png->Layout().totalBytes );
		CHECK( png->Decode( 0, pixels.data() ) );
		CHECK( pixels[ 0 ] == 255 && pixels[ 1 ] == 0 && pixels[ 2 ] == 0 && std::abs( pixels[ 3 ] - 128 ) <= 1 );
	}

	std::unique_ptr< FrameSource > jpeg = OpenFrameSource( Fixture( "gray.jpg" ), error );
	CHECK( jpeg != nullptr );
	if( jpeg )
	{
		CHECK( jpeg->IsStill() && jpeg->CodecName() == "JPEG" );
		CHECK( jpeg->Layout().shading == Shading::Yuv && jpeg->Layout().fullRange );
		std::vector< uint8_t > pixels( jpeg->Layout().totalBytes );
		CHECK( jpeg->Decode( 0, pixels.data() ) );
		CHECK( std::abs( pixels[ 0 ] - 128 ) <= 2 );
	}
}

// Resolume DXV 3 (frames cut from clips Alley encoded from a test pattern: colour bars along the top, the frame
// number in a black box, and in the alpha clips a band whose alpha runs from 0 at the left to 1 at the right):
// read by our own decoder, its textures make the right picture.
static void TestDxv()
{
	struct Case
	{
		const char* file;
		Shading shading;
		unsigned planes;
		const char* details;
		bool alpha;
		int tolerance; // DXT1 and DXT5 colours are coarser
	};
	const Case cases[] = {
		{ "dxv_dxt1.mov", Shading::Rgb, 1, "Normal Quality, on the GPU", false, 48 },
		{ "dxv_dxt5.mov", Shading::RgbaPremultiplied, 1, "Normal Quality with alpha, on the GPU", true, 40 }, // premultiplied: half alpha doubles the error
		{ "dxv_ycg6.mov", Shading::DxvYCoCg, 2, "High Quality, on the GPU", false, 12 },
		{ "dxv_yg10.mov", Shading::DxvYCoCgAlpha, 2, "High Quality with alpha, on the GPU", true, 12 },
	};
	struct Point
	{
		int x, y, r, g, b, a; // a: in the alpha clips; 255 otherwise
	};
	const Point points[] = {
		{ 480, 200, 0, 255, 0, 255 },   { 800, 200, 255, 255, 0, 255 }, { 1120, 200, 0, 0, 255, 255 }, { 1440, 200, 255, 0, 255, 255 },
		{ 1760, 200, 0, 255, 255, 255 }, { 70, 70, 0, 0, 0, 255 },       { 960, 700, 0, 0, 255, 127 },  { 1910, 700, 0, 255, 255, 254 },
		{ 10, 700, 255, 0, 0, 1 },
	};
	for( const Case& c : cases )
	{
		std::string error;
		std::unique_ptr< FrameSource > dxv = OpenFrameSource( Fixture( c.file ), error );
		CHECK( dxv != nullptr );
		if( !dxv )
			continue;
		const FrameLayout& layout = dxv->Layout();
		CHECK( dxv->CodecName() == "DXV" && dxv->Details() == c.details && dxv->IsGpuTexture() && !dxv->IsHap() );
		CHECK( dxv->FrameCount() == 2 && layout.width == 1920 && layout.height == 1080 );
		CHECK( layout.shading == c.shading && layout.planeCount == c.planes );
		CHECK( layout.planes[ 0 ].format == PlaneFormat::Compressed && layout.planes[ 0 ].width == 1920 && layout.planes[ 0 ].height == 1080 );
		if( c.planes == 2 )
			CHECK( layout.planes[ 1 ].width == 960 && layout.planes[ 1 ].height == 540 && layout.planes[ 1 ].bytes == 960 / 4 * 540 / 4 * 16 );
		std::vector< uint8_t > bytes( layout.totalBytes );
		for( size_t frame : { (size_t)1, (size_t)0 } ) // any order
		{
			CHECK( dxv->Decode( frame, bytes.data() ) );
			std::vector< uint8_t > rgba = bc::PictureRgba( layout, bytes.data() );
			CHECK( rgba.size() == (size_t)1920 * 1080 * 4 );
			if( rgba.empty() )
				continue;
			for( const Point& p : points )
			{
				const uint8_t* px = rgba.data() + ( (size_t)p.y * 1920 + p.x ) * 4;
				int a             = c.alpha ? p.a : 255;
				bool ok           = std::abs( px[ 3 ] - a ) <= 2;
				if( a >= 32 ) // colour means little where it is nearly transparent
					ok = ok && std::abs( px[ 0 ] - p.r ) <= c.tolerance && std::abs( px[ 1 ] - p.g ) <= c.tolerance && std::abs( px[ 2 ] - p.b ) <= c.tolerance;
				if( !ok )
				{
					std::fprintf( stderr, "%s frame %zu at %d,%d: %d %d %d %d, expected %d %d %d %d\n", c.file, frame, p.x, p.y, px[ 0 ], px[ 1 ], px[ 2 ], px[ 3 ],
					              p.r, p.g, p.b, a );
					++failures;
				}
			}
		}
		CHECK( !dxv->Decode( 2, bytes.data() ) ); // past the end
	}

	// Damaged frames are refused, never read past their end.
	std::unique_ptr< MovFile > movie = std::make_unique< MovFile >();
	CHECK( movie->Open( Fixture( "dxv_yg10.mov" ) ) && IsDxvCodec( movie->Video().codec ) );
	std::vector< uint8_t > frame;
	CHECK( movie->ReadSample( 0, frame ) );
	DxvLayout layout;
	CHECK( ProbeDxvFrame( frame.data(), movie->Video().samples[ 0 ].size, 1920, 1080, layout ) && layout.format == DxvFormat::Yg10 );
	std::vector< uint8_t > texture( layout.bytes ), chroma( layout.chromaBytes );
	for( size_t cut : { (size_t)16, frame.size() / 3, frame.size() - 1 } )
	{
		std::vector< uint8_t > shorter( frame.begin(), frame.begin() + (long)cut );
		CHECK( !ProbeDxvFrame( shorter.data(), shorter.size(), 1920, 1080, layout ) ); // the size in the header no longer matches
	}
	std::vector< uint8_t > scrambled( frame.begin(), frame.begin() + (long)movie->Video().samples[ 0 ].size );
	for( size_t i = 40; i < scrambled.size(); i += 97 )
		scrambled[ i ] ^= 0x5A;
	DecodeDxvFrame( scrambled.data(), scrambled.size(), layout, texture.data(), chroma.data() ); // may fail; must not crash
	CHECK( !ProbeDxvFrame( frame.data(), 8, 1920, 1080, layout ) );
}

int main()
{
	TestHapDxt1();
	TestHapAlpha();
	TestChunkedHapQ();
	TestThreadPool();
	TestBadFiles();
	TestFrameSourceHap();
	TestFrameSourceFfmpeg();
	TestDxv();
	if( failures )
	{
		std::fprintf( stderr, "%d check(s) failed\n", failures );
		return 1;
	}
	std::printf( "media tests passed\n" );
	return 0;
}
