// Measures how fast the media engine decodes a file, front to back, without a GPU: what the decode thread
// of a playing clip does. HAP decompresses its chunks on all cores; other formats go through FFmpeg.
// usage: decodebench <file> [--loops N] [--random]
//   --random: frames in random order, like scrubbing or jumping cue points
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <random>
#include <vector>
#include "media/FrameSource.h"

using namespace stltvj;
using Clock = std::chrono::steady_clock;

int main( int argc, char** argv )
{
	if( argc < 2 )
	{
		std::fprintf( stderr, "usage: decodebench <file> [--loops N] [--random]\n" );
		return 2;
	}
	int loops   = 1;
	bool random = false;
	for( int i = 2; i < argc; ++i )
	{
		if( !std::strcmp( argv[ i ], "--loops" ) && i + 1 < argc )
			loops = std::max( 1, std::atoi( argv[ ++i ] ) );
		else if( !std::strcmp( argv[ i ], "--random" ) )
			random = true;
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
