#pragma once
#include <cstddef>
#include <cstdint>

namespace stltvj
{
class ThreadPool;

// How a HAP frame decodes: one or two compressed GPU textures (HAP Q Alpha has YCoCg colour plus an RGTC1 alpha plane).
struct HapLayout
{
	unsigned textureCount = 0;
	unsigned formats[ 2 ] = {};  // HapTextureFormat values, which equal the OpenGL compressed formats except YCoCg
	size_t bytes[ 2 ]     = {};  // decoded size of each texture
	int chunks[ 2 ]       = {};  // independently decodable chunks per texture
	int paddedWidth       = 0;   // dimensions rounded up to whole 4x4 blocks
	int paddedHeight      = 0;

	size_t TotalBytes() const { return bytes[ 0 ] + bytes[ 1 ]; }
};

// Reads the frame headers; false if `data` is not a HAP frame we can decode.
bool ProbeHapFrame( const void* data, size_t size, int width, int height, HapLayout& layout );

// Decodes every texture of the frame into outputs[ i ] (each at least layout.bytes[ i ] long).
// Chunked frames are decompressed in parallel on `pool`.
bool DecodeHapFrame( const void* data, size_t size, const HapLayout& layout, void* const outputs[ 2 ], ThreadPool* pool );

// Bytes per 4x4 block of a HapTextureFormat.
size_t HapBlockBytes( unsigned format );
}
