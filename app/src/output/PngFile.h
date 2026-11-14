#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace stltvj
{
// A picture as a PNG file, with no library: 8-bit RGB, rows top to bottom (`bottomUp`: as OpenGL reads them),
// stored without compression. For snapshots, which are written once and opened anywhere.
std::vector< uint8_t > EncodePng( int width, int height, const std::vector< uint8_t >& rgb, bool bottomUp );
bool WritePng( const std::string& path, int width, int height, const std::vector< uint8_t >& rgb, bool bottomUp );
}
