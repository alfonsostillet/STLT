#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace stltvj
{
// Resolume's DXV 3 frames, taken apart only as far as the GPU needs: the outer compression is undone, and the
// textures inside (DXT/BC blocks) go to the GPU as they are, as with HAP.
//
//   DXT1 ("Normal Quality")         one DXT1 texture
//   DXT5 ("Normal Quality", alpha)   one DXT5 texture, colour premultiplied by alpha
//   YCG6 ("High Quality")            Y as an RGTC1 texture; Co and Cg as one RGTC2 texture at half size
//   YG10 ("High Quality", alpha)     Y and alpha as an RGTC2 texture; Co and Cg as above
//
// DXV has no published specification; the decompression follows FFmpeg's DXV decoder (libavcodec/dxv.c,
// LGPL 2.1 or later), which describes the format.
enum class DxvFormat
{
	Dxt1,
	Dxt5,
	Ycg6,
	Yg10,
};

struct DxvLayout
{
	DxvFormat format = DxvFormat::Dxt1;
	int width = 0, height = 0;           // the texture, whole 4x4 blocks
	int chromaWidth = 0, chromaHeight = 0; // the half-size Co/Cg texture (YCG6, YG10)
	size_t bytes       = 0;              // the main texture
	size_t chromaBytes = 0;              // the Co/Cg texture, 0 for DXT1 and DXT5
};

// Reads the frame header for a picture of width x height; false when it is not a DXV frame this reads.
bool ProbeDxvFrame( const uint8_t* data, size_t size, int width, int height, DxvLayout& layout );
// Undoes the frame's compression into `texture` (layout.bytes) and `chroma` (layout.chromaBytes). The
// decompression reads back what it wrote, so the outputs should be ordinary memory, not a mapped GPU buffer.
bool DecodeDxvFrame( const uint8_t* data, size_t size, const DxvLayout& layout, uint8_t* texture, uint8_t* chroma );

bool IsDxvCodec( uint32_t fourcc );
}
