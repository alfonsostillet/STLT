#pragma once
#include <memory>
#include <string>

namespace stltvj
{
// A clip's sound, decoded to interleaved float stereo at the rate the audio device runs at (through FFmpeg).
class AudioDecoder
{
public:
	AudioDecoder();
	~AudioDecoder();
	AudioDecoder( const AudioDecoder& ) = delete;
	AudioDecoder& operator=( const AudioDecoder& ) = delete;

	// False when the file has no sound (or the build has no FFmpeg).
	bool Open( const std::string& path, int sampleRate );
	// Up to `frames` stereo frames into `out`; fewer only at the end of the sound.
	int Read( float* out, int frames );
	// The next Read starts at `seconds`, to the sample.
	void Seek( double seconds );
	double Position() const; // seconds of the next frame Read returns
	double Duration() const;
	int SampleRate() const;

private:
	struct Impl;
	std::unique_ptr< Impl > impl;
};
}
