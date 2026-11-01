#include "media/DxvSource.h"
#include <hap.h>
#include <cstring>
#include <vector>

namespace stltvj
{
namespace
{
constexpr unsigned kRgtc2 = 0x8DBD; // GL_COMPRESSED_RG_RGTC2 (BC5): two channels, each an RGTC1 block
}

bool DxvSource::Open( std::unique_ptr< MovFile > file, std::string& error )
{
	movie                   = std::move( file );
	const VideoTrack& video = movie->Video();
	std::vector< uint8_t > first;
	if( video.samples.empty() || !movie->ReadSample( 0, first ) || !ProbeDxvFrame( first.data(), video.samples[ 0 ].size, video.width, video.height, dxv ) )
	{
		error = "Not a DXV 3 frame this player reads";
		return false;
	}
	layout        = FrameLayout{};
	layout.width  = video.width;
	layout.height = video.height;
	Plane main;
	main.format = PlaneFormat::Compressed;
	main.width  = dxv.width;
	main.height = dxv.height;
	main.bytes  = dxv.bytes;
	switch( dxv.format )
	{
	case DxvFormat::Dxt1:
		main.compressedFormat = HapTextureFormat_RGB_DXT1;
		layout.shading        = Shading::Rgb;
		break;
	case DxvFormat::Dxt5:
		main.compressedFormat = HapTextureFormat_RGBA_DXT5;
		layout.shading        = Shading::RgbaPremultiplied;
		break;
	case DxvFormat::Ycg6:
		main.compressedFormat = HapTextureFormat_A_RGTC1; // Y
		layout.shading        = Shading::DxvYCoCg;
		break;
	case DxvFormat::Yg10:
		main.compressedFormat = kRgtc2; // Y and alpha
		layout.shading        = Shading::DxvYCoCgAlpha;
		break;
	}
	layout.Add( main );
	if( dxv.chromaBytes )
	{
		Plane chroma;
		chroma.format           = PlaneFormat::Compressed;
		chroma.compressedFormat = kRgtc2; // Co and Cg
		chroma.width            = dxv.chromaWidth;
		chroma.height           = dxv.chromaHeight;
		chroma.bytes            = dxv.chromaBytes;
		layout.Add( chroma );
	}
	return true;
}

std::string DxvSource::CodecName() const
{
	return "DXV";
}

std::string DxvSource::Details() const
{
	switch( dxv.format )
	{
	case DxvFormat::Dxt1: return "Normal Quality, on the GPU";
	case DxvFormat::Dxt5: return "Normal Quality with alpha, on the GPU";
	case DxvFormat::Ycg6: return "High Quality, on the GPU";
	case DxvFormat::Yg10: return "High Quality with alpha, on the GPU";
	}
	return {};
}

bool DxvSource::Decode( size_t frame, uint8_t* destination )
{
	const VideoTrack& video = movie->Video();
	if( frame >= video.samples.size() )
		return false;
	// Per thread, so several threads can decode different frames at once. The textures are decompressed into
	// ordinary memory first (the decompression reads back what it wrote) and then copied to the destination.
	thread_local std::vector< uint8_t > compressed, texture, chroma;
	if( !movie->ReadSample( frame, compressed ) )
		return false;
	size_t size = video.samples[ frame ].size;
	DxvLayout frameLayout;
	if( !ProbeDxvFrame( compressed.data(), size, video.width, video.height, frameLayout ) || frameLayout.format != dxv.format
	    || frameLayout.bytes != dxv.bytes || frameLayout.chromaBytes != dxv.chromaBytes )
		return false;
	texture.resize( dxv.bytes );
	chroma.resize( dxv.chromaBytes );
	if( !DecodeDxvFrame( compressed.data(), size, frameLayout, texture.data(), chroma.data() ) )
		return false;
	std::memcpy( destination, texture.data(), texture.size() );
	if( !chroma.empty() )
		std::memcpy( destination + layout.planes[ 1 ].offset, chroma.data(), chroma.size() );
	return true;
}
}
