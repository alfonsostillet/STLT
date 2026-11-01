// Measures how fast the media engine decodes a file, front to back, without a GPU: what the decode thread
// of a playing clip does. HAP decompresses its chunks on all cores; other formats go through FFmpeg.
// usage: decodebench <file> [--loops N] [--random] [--scratch [--exact]] [--dump FRAME OUT.rgba]
//   --random: frames in random order, like scrubbing or jumping cue points
//   --scratch: a hand scratching back and forth over 4 seconds of the clip's middle for 6 seconds, as the player asks for frames (giving up on frames
//              left behind, nearest frame while moving); prints how far behind the hand the picture was.
//              --exact: every frame decoded exactly, the old way, for comparison
//   --dump: writes the picture of FRAME, as the GPU would draw it, as raw RGBA (HAP and DXV)
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <numeric>
#include <random>
#include <vector>
#include "BcDecode.h"
#include "media/FrameSource.h"

using namespace stltvj;
using Clock = std::chrono::steady_clock;

// The hand: back and forth over four seconds in the middle of the clip, a full swing every two seconds, resting for a quarter
// second at each end.
static int64_t Hand( double t, double fps, size_t count )
{
	double phase = std::fmod( t, 2.5 ) / 2.5;
	double swing = phase < 0.4 ? phase / 0.4 : phase < 0.5 ? 1 : phase < 0.9 ? 1 - ( phase - 0.5 ) / 0.4 : 0;
	double eased = 0.5 - 0.5 * std::cos( swing * 3.14159265 );
	int64_t first = std::max< int64_t >( 0, (int64_t)count / 2 - (int64_t)( 2 * fps ) ); // across the middle
	int64_t frame = first + (int64_t)( 4.0 * eased * fps );
	return std::min< int64_t >( frame, (int64_t)count - 1 );
}

static int Scratch( FrameSource& source, bool exact )
{
	std::vector< uint8_t > frame( source.Layout().totalBytes );
	double fps = source.FrameRate();
	Clock::time_point start = Clock::now();
	auto now   = [ & ] { return std::chrono::duration< double >( Clock::now() - start ).count(); };
	int64_t shown = -1;
	double lagSum = 0, lagMax = 0, last = 0, lastShow = 0, worstWait = 0;
	int updates = 0;
	const double kLength = 6.0;
	auto account = [ & ]( double t ) {
		// The picture was `shown` from `last` to `t`; how far behind the hand, sampled every millisecond.
		for( double s = last; s < t && shown >= 0; s += 0.001 )
		{
			double lag = (double)std::llabs( Hand( s, fps, source.FrameCount() ) - shown );
			lagSum += lag * std::min( 0.001, t - s );
			lagMax = std::max( lagMax, lag );
		}
		last = t;
	};
	while( now() < kLength )
	{
		int64_t wanted = Hand( now(), fps, source.FrameCount() );
		int64_t decoded = wanted;
		DecodeResult result;
		if( exact )
			result = source.Decode( (size_t)wanted, frame.data() ) ? DecodeResult::Exact : DecodeResult::Failed;
		else
		{
			// As the player: give up when the hand has moved on, nearby frames while it moves (it always moves
			// here except at the ends, where the exact frame is wanted).
			double asked = now();
			DecodeRequest request;
			request.abandon     = [ & ] { return Hand( now(), fps, source.FrameCount() ) != wanted; };
			request.approximate = [ & ] { return now() - asked < 0.25 || Hand( now() - 0.02, fps, source.FrameCount() ) != wanted; };
			request.shown       = shown;
			result              = source.DecodeFrame( (size_t)wanted, frame.data(), request, decoded );
		}
		double t = now();
		account( t );
		if( result == DecodeResult::Failed )
		{
			std::fprintf( stderr, "decode failed at frame %lld\n", (long long)wanted );
			return 1;
		}
		if( result != DecodeResult::Abandoned && decoded != shown )
		{
			worstWait = std::max( worstWait, t - lastShow );
			lastShow  = t;
			shown     = decoded;
			++updates;
		}
	}
	account( kLength );
	std::printf( "scratch %s  behind the hand: %.1f frames on average, %.0f at worst;  %.1f pictures a second, longest wait %.0f ms\n",
	             exact ? "(exact)" : "        ", lagSum / kLength, lagMax, updates / kLength, worstWait * 1000 );
	return 0;
}

int main( int argc, char** argv )
{
	if( argc < 2 )
	{
		std::fprintf( stderr, "usage: decodebench <file> [--loops N] [--random]\n" );
		return 2;
	}
	int loops    = 1;
	bool random  = false;
	bool scratch = false, exact = false;
	long dumpFrame = -1;
	const char* dumpPath = nullptr;
	for( int i = 2; i < argc; ++i )
	{
		if( !std::strcmp( argv[ i ], "--loops" ) && i + 1 < argc )
			loops = std::max( 1, std::atoi( argv[ ++i ] ) );
		else if( !std::strcmp( argv[ i ], "--random" ) )
			random = true;
		else if( !std::strcmp( argv[ i ], "--scratch" ) )
			scratch = true;
		else if( !std::strcmp( argv[ i ], "--exact" ) )
			exact = true;
		else if( !std::strcmp( argv[ i ], "--dump" ) && i + 2 < argc )
		{
			dumpFrame = std::atol( argv[ ++i ] );
			dumpPath  = argv[ ++i ];
		}
	}

	std::string error;
	std::unique_ptr< FrameSource > source = OpenFrameSource( argv[ 1 ], error );
	if( !source )
	{
		std::fprintf( stderr, "%s: %s\n", argv[ 1 ], error.c_str() );
		return 1;
	}
	const FrameLayout& layout = source->Layout();
	std::printf( "%s  %dx%d  %s  %zu frames  %.3f fps  %s\n", argv[ 1 ], layout.width, layout.height, source->CodecName().c_str(), source->FrameCount(),
	             source->FrameRate(), source->Details().c_str() );

	if( scratch )
		return Scratch( *source, exact );
	if( dumpPath )
	{
		std::vector< uint8_t > bytes( layout.totalBytes );
		std::vector< uint8_t > rgba;
		if( !source->Decode( (size_t)dumpFrame, bytes.data() ) || ( rgba = bc::PictureRgba( layout, bytes.data() ) ).empty() )
		{
			std::fprintf( stderr, "cannot dump frame %ld\n", dumpFrame );
			return 1;
		}
		FILE* out = std::fopen( dumpPath, "wb" );
		if( !out || std::fwrite( rgba.data(), 1, rgba.size(), out ) != rgba.size() )
			return 1;
		std::fclose( out );
		std::printf( "wrote %dx%d RGBA to %s\n", layout.width, layout.height, dumpPath );
		return 0;
	}

	std::vector< size_t > order( source->FrameCount() );
	std::iota( order.begin(), order.end(), 0 );
	if( random )
		std::shuffle( order.begin(), order.end(), std::mt19937( 1 ) );

	std::vector< uint8_t > frame( layout.totalBytes );
	size_t frames = 0;
	Clock::time_point start = Clock::now();
	for( int loop = 0; loop < loops; ++loop )
		for( size_t f : order )
		{
			if( !source->Decode( f, frame.data() ) )
			{
				std::fprintf( stderr, "decode failed at frame %zu\n", f );
				return 1;
			}
			++frames;
		}
	double seconds = std::chrono::duration< double >( Clock::now() - start ).count();
	std::printf( "%s  %8.3f ms/frame  %8.1f fps  (%.1f MB per frame to the GPU)\n", random ? "random" : "in order", 1000 * seconds / frames, frames / seconds,
	             layout.totalBytes / 1e6 );
	return 0;
}
