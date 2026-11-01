#include <atomic>
#include <cstdlib>
#include <cstring>
#include "HapSource.h"
#include <hap.h>
#include <algorithm>
#include <cctype>
#include <vector>
#include "media/ThreadPool.h"

namespace stltvj
{
void FrameLayout::Add( const Plane& plane )
{
	if( planeCount >= 3 )
		return;
	Plane p               = plane;
	p.offset              = totalBytes;
	planes[ planeCount++ ] = p;
	totalBytes += p.bytes;
}

bool IsMediaExtension( const std::string& extension )
{
	static const char* known[] = { ".mov", ".mp4", ".m4v", ".avi", ".mkv", ".webm", ".mpg", ".mpeg", ".ts", ".mxf", ".gif",
		                           ".png", ".jpg", ".jpeg", ".bmp", ".tif", ".tiff", ".webp" };
	std::string e = extension;
	std::transform( e.begin(), e.end(), e.begin(), []( unsigned char c ) { return (char)std::tolower( c ); } );
	for( const char* k : known )
		if( e == k )
			return true;
	return false;
}

bool IsAudioExtension( const std::string& extension )
{
	static const char* known[] = { ".mp3", ".wav", ".aif", ".aiff", ".flac", ".m4a", ".aac", ".ogg", ".opus", ".wma" };
	std::string e = extension;
	std::transform( e.begin(), e.end(), e.begin(), []( unsigned char c ) { return (char)std::tolower( c ); } );
	for( const char* k : known )
		if( e == k )
			return true;
	return false;
}

bool HapSource::Open( std::unique_ptr< MovFile > file, std::string& error )
{
	movie                   = std::move( file );
	const VideoTrack& video = movie->Video();
	std::vector< uint8_t > first;
	if( !movie->ReadSample( 0, first ) || !ProbeHapFrame( first.data(), video.samples[ 0 ].size, video.width, video.height, hap ) )
	{
		error = "The first frame is not valid HAP";
		return false;
	}
	layout        = FrameLayout{};
	layout.width  = video.width;
	layout.height = video.height;
	for( unsigned i = 0; i < hap.textureCount; ++i )
	{
		Plane p;
		p.format           = PlaneFormat::Compressed;
		p.compressedFormat = hap.formats[ i ];
		p.width            = hap.paddedWidth;
		p.height           = hap.paddedHeight;
		p.bytes            = hap.bytes[ i ];
		layout.Add( p );
	}
	unsigned f0 = hap.formats[ 0 ];
	if( hap.textureCount == 2 )
		layout.shading = Shading::YCoCgAlpha;
	else if( f0 == HapTextureFormat_YCoCg_DXT5 )
		layout.shading = Shading::YCoCg;
	else if( f0 == HapTextureFormat_A_RGTC1 )
		layout.shading = Shading::AlphaOnly;
	else if( f0 == HapTextureFormat_RGBA_DXT5 || f0 == HapTextureFormat_RGBA_BPTC_UNORM )
		layout.shading = Shading::Rgba;
	else
		layout.shading = Shading::Rgb;
	return true;
}

std::string HapSource::CodecName() const
{
	const char* name = HapCodecName( movie->Video().codec );
	return name ? name : "HAP";
}

std::string HapSource::Details() const
{
	return std::to_string( hap.chunks[ 0 ] ) + ( hap.chunks[ 0 ] == 1 ? " chunk" : " chunks" );
}

bool HapSource::Decode( size_t frame, uint8_t* destination )
{
	const VideoTrack& video = movie->Video();
	if( frame >= video.samples.size() )
		return false;
	// Each call has its own read buffer, so several threads can decode different frames at once.
	thread_local std::vector< uint8_t > compressed;
	if( !movie->ReadSample( frame, compressed ) )
		return false;
	size_t size = video.samples[ frame ].size;
	HapLayout frameLayout;
	if( !ProbeHapFrame( compressed.data(), size, video.width, video.height, frameLayout ) || frameLayout.textureCount != hap.textureCount
	    || frameLayout.formats[ 0 ] != hap.formats[ 0 ] || frameLayout.formats[ 1 ] != hap.formats[ 1 ] )
		return false;
	void* outputs[ 2 ] = { destination, destination + layout.planes[ 0 ].bytes };
	return DecodeHapFrame( compressed.data(), size, frameLayout, outputs, &ThreadPool::Shared() );
}

std::unique_ptr< FrameSource > OpenFfmpegSource( const std::string& path, std::string& error );

std::unique_ptr< FrameSource > OpenFrameSource( const std::string& path, std::string& error )
{
	auto movie = std::make_unique< MovFile >();
	if( movie->Open( path ) && IsHapCodec( movie->Video().codec ) )
	{
		auto source = std::make_unique< HapSource >();
		if( source->Open( std::move( movie ), error ) )
			return source;
		return nullptr;
	}
#ifdef STLTVJ_WITH_FFMPEG
	return OpenFfmpegSource( path, error );
#else
	std::string codec = movie->Video().codec ? FourccString( movie->Video().codec ) : "";
	error = codec.empty() ? movie->Error() : "Codec " + codec + " needs FFmpeg, which this build does not include: convert the clip to HAP";
	return nullptr;
#endif
}

namespace
{
std::atomic< bool > hardwareDecoding{ [] {
	const char* env = std::getenv( "STLTVJ_HWDEC" );
	return !env || std::strcmp( env, "0" ) != 0;
}() };
std::atomic< int > frameCacheMegabytes{ 256 };
}

void SetFrameCacheMegabytes( int megabytes )
{
	frameCacheMegabytes = std::max( 0, megabytes );
}

int FrameCacheMegabytes()
{
	return frameCacheMegabytes;
}

void SetHardwareDecoding( bool on )
{
	hardwareDecoding = on;
}

bool HardwareDecoding()
{
	return hardwareDecoding;
}

bool HasFfmpeg()
{
#ifdef STLTVJ_WITH_FFMPEG
	return true;
#else
	return false;
#endif
}
}
