// Measures how fast the media engine reads and decodes a HAP movie, without a GPU.
// usage: hapbench <movie.mov> [--threads N] [--loops N]
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
#include "media/HapFrame.h"
#include "media/MovFile.h"
#include "media/ThreadPool.h"

using namespace stltvj;
using Clock = std::chrono::steady_clock;

static double Seconds( Clock::time_point since )
{
	return std::chrono::duration< double >( Clock::now() - since ).count();
}

int main( int argc, char** argv )
{
	if( argc < 2 )
	{
		std::fprintf( stderr, "usage: hapbench <movie.mov> [--threads N] [--loops N]\n" );
		return 2;
	}
	int threads = 0;
	int loops   = 1;
	for( int i = 2; i + 1 < argc; i += 2 )
	{
		if( !std::strcmp( argv[ i ], "--threads" ) )
			threads = std::atoi( argv[ i + 1 ] );
		else if( !std::strcmp( argv[ i ], "--loops" ) )
			loops = std::max( 1, std::atoi( argv[ i + 1 ] ) );
	}

	MovFile movie;
	if( !movie.Open( argv[ 1 ] ) )
	{
		std::fprintf( stderr, "%s: %s\n", argv[ 1 ], movie.Error().c_str() );
		return 1;
	}
	const VideoTrack& video = movie.Video();
	std::printf( "%s  %dx%d  %s  %zu frames  %.3f fps\n", argv[ 1 ], video.width, video.height,
	             HapCodecName( video.codec ) ? HapCodecName( video.codec ) : FourccString( video.codec ).c_str(), video.samples.size(), video.FrameRate() );
	if( !IsHapCodec( video.codec ) )
	{
		std::fprintf( stderr, "Not a HAP movie\n" );
		return 1;
	}

	std::unique_ptr< ThreadPool > pool;
	if( threads != 1 )
		pool = std::make_unique< ThreadPool >( threads > 1 ? (unsigned)threads - 1 : 0 );

	std::vector< uint8_t > compressed;
	std::vector< uint8_t > decoded;
	double readTime = 0, decodeTime = 0;
	size_t compressedBytes = 0, frames = 0;
	HapLayout layout;
	for( int loop = 0; loop < loops; ++loop )
		for( size_t f = 0; f < video.samples.size(); ++f )
		{
			Clock::time_point start = Clock::now();
			if( !movie.ReadSample( f, compressed ) )
			{
				std::fprintf( stderr, "read failed at frame %zu\n", f );
				return 1;
			}
			readTime += Seconds( start );
			size_t size = video.samples[ f ].size;
			if( !ProbeHapFrame( compressed.data(), size, video.width, video.height, layout ) )
			{
				std::fprintf( stderr, "frame %zu is not a valid HAP frame\n", f );
				return 1;
			}
			if( decoded.size() < layout.TotalBytes() )
				decoded.resize( layout.TotalBytes() );
			void* outputs[ 2 ] = { decoded.data(), decoded.data() + layout.bytes[ 0 ] };
			start              = Clock::now();
			if( !DecodeHapFrame( compressed.data(), size, layout, outputs, pool.get() ) )
			{
				std::fprintf( stderr, "decode failed at frame %zu\n", f );
				return 1;
			}
			decodeTime += Seconds( start );
			compressedBytes += size;
			++frames;
		}
	std::printf( "textures %u  chunks %d  threads %u\n", layout.textureCount, layout.chunks[ 0 ], pool ? pool->WorkerCount() + 1 : 1 );
	std::printf( "read    %8.3f ms/frame  %8.1f MB/s\n", 1000 * readTime / frames, compressedBytes / readTime / 1e6 );
	std::printf( "decode  %8.3f ms/frame  %8.1f fps\n", 1000 * decodeTime / frames, frames / decodeTime );
	return 0;
}
