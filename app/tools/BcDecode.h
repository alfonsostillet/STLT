#pragma once
// Texture blocks decoded on the CPU, for the tests and decodebench --dump: what the GPU does with DXT1, DXT5,
// RGTC1 and RGTC2, to check the textures a source hands over against the pictures they should make.
#include <algorithm>
#include <cstdint>
#include <vector>
#include "media/FrameSource.h"

namespace bc
{
// The clip shader's conversions (Compositor.cpp), for DXV: premultiplied DXT5 to straight, and DXV High Quality's
// YCoCg (Co and Cg at half scale, 127 for zero) to RGB.
inline uint8_t Clamp( double v )
{
	return (uint8_t)( v < 0 ? 0 : v > 255 ? 255 : v + 0.5 );
}
inline void DxvYCoCg( int y, int co, int cg, uint8_t* rgb )
{
	double c = ( co - 127 ) * 2.0, g = ( cg - 127 ) * 2.0;
	rgb[ 0 ] = Clamp( y + c - g );
	rgb[ 1 ] = Clamp( y + g );
	rgb[ 2 ] = Clamp( y - c - g );
}

inline void Colour565( uint16_t c, int rgb[ 3 ] )
{
	rgb[ 0 ] = ( ( c >> 11 ) & 31 ) * 255 / 31;
	rgb[ 1 ] = ( ( c >> 5 ) & 63 ) * 255 / 63;
	rgb[ 2 ] = ( c & 31 ) * 255 / 31;
}

// One DXT1 colour block into a 4x4 RGBA patch (stride in pixels); DXT5's colour half when `dxt5`.
inline void ColourBlock( const uint8_t* b, uint8_t* out, int stride, bool dxt5 )
{
	uint16_t c0 = (uint16_t)( b[ 0 ] | b[ 1 ] << 8 ), c1 = (uint16_t)( b[ 2 ] | b[ 3 ] << 8 );
	int p[ 4 ][ 4 ];
	Colour565( c0, p[ 0 ] );
	Colour565( c1, p[ 1 ] );
	bool four = dxt5 || c0 > c1;
	for( int k = 0; k < 3; ++k )
	{
		p[ 2 ][ k ] = four ? ( 2 * p[ 0 ][ k ] + p[ 1 ][ k ] ) / 3 : ( p[ 0 ][ k ] + p[ 1 ][ k ] ) / 2;
		p[ 3 ][ k ] = four ? ( p[ 0 ][ k ] + 2 * p[ 1 ][ k ] ) / 3 : 0;
	}
	p[ 0 ][ 3 ] = p[ 1 ][ 3 ] = p[ 2 ][ 3 ] = 255;
	p[ 3 ][ 3 ] = four ? 255 : 0;
	uint32_t bits = (uint32_t)( b[ 4 ] | b[ 5 ] << 8 | b[ 6 ] << 16 | (uint32_t)b[ 7 ] << 24 );
	for( int i = 0; i < 16; ++i )
	{
		int* c     = p[ ( bits >> ( 2 * i ) ) & 3 ];
		uint8_t* o = out + ( ( i / 4 ) * stride + i % 4 ) * 4;
		for( int k = 0; k < 4; ++k )
			o[ k ] = (uint8_t)c[ k ];
	}
}

// One RGTC1 (BC4) block, also DXT5's alpha half, into 16 values.
inline void Rgtc1Block( const uint8_t* b, uint8_t out[ 16 ] )
{
	int a0 = b[ 0 ], a1 = b[ 1 ], v[ 8 ] = { a0, a1 };
	for( int i = 1; i < 7; ++i )
		v[ i + 1 ] = a0 > a1 ? ( ( 7 - i ) * a0 + i * a1 ) / 7 : i < 5 ? ( ( 5 - i ) * a0 + i * a1 ) / 5 : i == 5 ? 0 : 255;
	uint64_t bits = 0;
	for( int i = 0; i < 6; ++i )
		bits |= (uint64_t)b[ 2 + i ] << ( 8 * i );
	for( int i = 0; i < 16; ++i )
		out[ i ] = (uint8_t)v[ ( bits >> ( 3 * i ) ) & 7 ];
}

// A whole DXT1 or DXT5 texture of width x height (multiples of 4) into RGBA.
inline std::vector< uint8_t > DecodeDxt( const uint8_t* data, int width, int height, bool dxt5 )
{
	std::vector< uint8_t > rgba( (size_t)width * height * 4 );
	size_t blockBytes = dxt5 ? 16 : 8;
	for( int by = 0; by < height / 4; ++by )
		for( int bx = 0; bx < width / 4; ++bx )
		{
			const uint8_t* b = data + ( (size_t)by * ( width / 4 ) + bx ) * blockBytes;
			uint8_t* out     = rgba.data() + ( (size_t)by * 4 * width + bx * 4 ) * 4;
			ColourBlock( dxt5 ? b + 8 : b, out, width, dxt5 );
			if( dxt5 )
			{
				uint8_t a[ 16 ];
				Rgtc1Block( b, a );
				for( int i = 0; i < 16; ++i )
					out[ ( ( i / 4 ) * width + i % 4 ) * 4 + 3 ] = a[ i ];
			}
		}
	return rgba;
}

// One channel of an RGTC1 texture, or channel `channel` (0 or 1) of an RGTC2 one, into a plane of bytes.
inline std::vector< uint8_t > DecodeRgtc( const uint8_t* data, int width, int height, bool rgtc2, int channel )
{
	std::vector< uint8_t > plane( (size_t)width * height );
	size_t blockBytes = rgtc2 ? 16 : 8;
	for( int by = 0; by < height / 4; ++by )
		for( int bx = 0; bx < width / 4; ++bx )
		{
			uint8_t v[ 16 ];
			Rgtc1Block( data + ( (size_t)by * ( width / 4 ) + bx ) * blockBytes + 8 * channel, v );
			for( int i = 0; i < 16; ++i )
				plane[ (size_t)( by * 4 + i / 4 ) * width + bx * 4 + i % 4 ] = v[ i ];
		}
	return plane;
}

// The picture a frame of a texture source makes, as straight RGBA of the picture's size, as the clip shader does
// it (chroma read at the nearest texel); empty for a layout this does not know.
inline std::vector< uint8_t > PictureRgba( const stltvj::FrameLayout& layout, const uint8_t* bytes )
{
	using stltvj::Shading;
	const stltvj::Plane& p0 = layout.planes[ 0 ];
	int W = layout.width, H = layout.height;
	std::vector< uint8_t > out( (size_t)W * H * 4 );
	std::vector< uint8_t > full, y, a, co, cg;
	switch( layout.shading )
	{
	case Shading::Rgb:
	case Shading::Rgba:
	case Shading::RgbaPremultiplied:
		full = DecodeDxt( bytes, p0.width, p0.height, layout.shading != Shading::Rgb );
		break;
	case Shading::DxvYCoCg:
	case Shading::DxvYCoCgAlpha:
	{
		bool alpha                 = layout.shading == Shading::DxvYCoCgAlpha;
		const stltvj::Plane& p1    = layout.planes[ 1 ];
		y                          = DecodeRgtc( bytes, p0.width, p0.height, alpha, 0 );
		if( alpha )
			a = DecodeRgtc( bytes, p0.width, p0.height, true, 1 );
		co = DecodeRgtc( bytes + p1.offset, p1.width, p1.height, true, 0 );
		cg = DecodeRgtc( bytes + p1.offset, p1.width, p1.height, true, 1 );
		break;
	}
	default: return {};
	}
	for( int j = 0; j < H; ++j )
		for( int i = 0; i < W; ++i )
		{
			uint8_t* o = out.data() + ( (size_t)j * W + i ) * 4;
			if( !full.empty() )
			{
				const uint8_t* c = full.data() + ( (size_t)j * p0.width + i ) * 4;
				int alpha        = layout.shading == Shading::Rgb ? 255 : c[ 3 ];
				for( int k = 0; k < 3; ++k )
					o[ k ] = layout.shading == Shading::RgbaPremultiplied ? ( alpha ? Clamp( c[ k ] * 255.0 / alpha ) : 0 ) : c[ k ];
				o[ 3 ] = (uint8_t)alpha;
				continue;
			}
			const stltvj::Plane& p1 = layout.planes[ 1 ];
			int ci = std::min( p1.width - 1, i * p1.width / p0.width ), cj = std::min( p1.height - 1, j * p1.height / p0.height );
			size_t at = (size_t)cj * p1.width + ci;
			DxvYCoCg( y[ (size_t)j * p0.width + i ], co[ at ], cg[ at ], o );
			o[ 3 ] = a.empty() ? 255 : a[ (size_t)j * p0.width + i ];
		}
	return out;
}
}
