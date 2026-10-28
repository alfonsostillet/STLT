#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "FileReader.h"

namespace stltvj
{
// One video frame inside the file.
struct Sample
{
	uint64_t offset = 0;
	uint32_t size   = 0;
	int64_t time    = 0; // presentation start, in track timescale units
};

// The first video track of a QuickTime (.mov) or MP4 file: just what a player needs to fetch frames.
struct VideoTrack
{
	uint32_t codec = 0; // sample description fourcc, e.g. 'HapY'
	int width      = 0;
	int height     = 0;
	uint32_t timescale = 0;
	int64_t duration   = 0; // in timescale units
	std::vector< Sample > samples;

	double DurationSeconds() const { return timescale ? (double)duration / timescale : 0.0; }
	double FrameRate() const;
	// The frame showing at `seconds` from the start (clamped to the track).
	size_t FrameAt( double seconds ) const;
	double FrameTime( size_t frame ) const;
};

std::string FourccString( uint32_t fourcc );
bool IsHapCodec( uint32_t fourcc );
const char* HapCodecName( uint32_t fourcc );

class MovFile
{
public:
	// Opens the file and reads its sample tables; error() says why on failure.
	bool Open( const std::string& path );
	const std::string& Error() const { return error; }
	const VideoTrack& Video() const { return video; }
	const FileReader& Reader() const { return reader; }

	// Reads a frame's compressed bytes into `buffer` (resized as needed).
	bool ReadSample( size_t frame, std::vector< uint8_t >& buffer ) const;

private:
	bool Fail( const std::string& message );
	bool ParseMovie( const std::vector< uint8_t >& moov );

	FileReader reader;
	VideoTrack video;
	std::string error;
};
}
