#include "output/PngFile.h"
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace stltvj
{
namespace
{
uint32_t Crc( const uint8_t* data, size_t size, uint32_t crc = 0xFFFFFFFFu )
{
	static uint32_t table[ 256 ];
	static bool made = false;
	if( !made )
	{
		for( uint32_t n = 0; n < 256; ++n )
		{
			uint32_t c = n;
			for( int k = 0; k < 8; ++k )
				c = c & 1 ? 0xEDB88320u ^ ( c >> 1 ) : c >> 1;
			table[ n ] = c;
		}
		made = true;
	}
	for( size_t i = 0; i < size; ++i )
		crc = table[ ( crc ^ data[ i ] ) & 0xFF ] ^ ( crc >> 8 );
	return crc;
}

void Put32( std::vector< uint8_t >& out, uint32_t v )
{
	out.push_back( (uint8_t)( v >> 24 ) );
	out.push_back( (uint8_t)( v >> 16 ) );
	out.push_back( (uint8_t)( v >> 8 ) );
	out.push_back( (uint8_t)v );
}

void Chunk( std::vector< uint8_t >& out, const char* type, const std::vector< uint8_t >& data )
{
	Put32( out, (uint32_t)data.size() );
	size_t start = out.size();
	out.insert( out.end(), type, type + 4 );
	out.insert( out.end(), data.begin(), data.end() );
	Put32( out, Crc( out.data() + start, out.size() - start ) ^ 0xFFFFFFFFu );
}
}

std::vector< uint8_t > EncodePng( int width, int height, const std::vector< uint8_t >& rgb, bool bottomUp )
{
	std::vector< uint8_t > png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
	if( width <= 0 || height <= 0 || rgb.size() < (size_t)width * height * 3 )
		return {};
	std::vector< uint8_t > header;
	Put32( header, (uint32_t)width );
	Put32( header, (uint32_t)height );
	header.insert( header.end(), { 8, 2, 0, 0, 0 } ); // 8 bits, RGB, deflate, no filter, not interlaced
	Chunk( png, "IHDR", header );

	// The rows, each after its filter byte (0: none), in zlib's stored blocks of at most 65535 bytes.
	size_t row = (size_t)width * 3;
	std::vector< uint8_t > raw;
	raw.reserve( ( row + 1 ) * height );
	for( int y = 0; y < height; ++y )
	{
		raw.push_back( 0 );
		const uint8_t* from = rgb.data() + ( bottomUp ? (size_t)( height - 1 - y ) : (size_t)y ) * row;
		raw.insert( raw.end(), from, from + row );
	}
	std::vector< uint8_t > z = { 0x78, 0x01 };
	for( size_t at = 0; at < raw.size() || raw.empty(); )
	{
		size_t n   = std::min< size_t >( 65535, raw.size() - at );
		bool last  = at + n == raw.size();
		z.push_back( last ? 1 : 0 );
		z.push_back( (uint8_t)n );
		z.push_back( (uint8_t)( n >> 8 ) );
		z.push_back( (uint8_t)~n );
		z.push_back( (uint8_t)( ~n >> 8 ) );
		z.insert( z.end(), raw.begin() + at, raw.begin() + at + n );
		at += n;
		if( last )
			break;
	}
	uint32_t a = 1, b = 0; // Adler-32
	for( uint8_t v : raw )
	{
		a = ( a + v ) % 65521;
		b = ( b + a ) % 65521;
	}
	Put32( z, ( b << 16 ) | a );
	Chunk( png, "IDAT", z );
	Chunk( png, "IEND", {} );
	return png;
}

bool WritePng( const std::string& path, int width, int height, const std::vector< uint8_t >& rgb, bool bottomUp )
{
	std::vector< uint8_t > png = EncodePng( width, height, rgb, bottomUp );
	if( png.empty() )
		return false;
	std::ofstream out( std::filesystem::u8path( path ), std::ios::binary | std::ios::trunc ); // u8path: accents on Windows
	out.write( (const char*)png.data(), (std::streamsize)png.size() );
	return (bool)out;
}
}
