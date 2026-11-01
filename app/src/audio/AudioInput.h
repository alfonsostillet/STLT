#pragma once
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace stltvj
{
// The sound coming in (a microphone, a line in, the mixer's feed), measured for parameters to follow: the overall
// level and three bands. Opened only while some animation listens (Animation's audio modes).
class AudioInput
{
public:
	enum Band
	{
		Level,  // the whole signal
		Bass,   // 20..200 Hz
		Mid,    // 200..2000 Hz
		Treble, // 2..12 kHz
		BandCount
	};
	static const char* BandName( Band band );

	AudioInput();
	~AudioInput();
	AudioInput( const AudioInput& ) = delete;
	AudioInput& operator=( const AudioInput& ) = delete;

	static std::vector< std::string > Devices();
	// The named input, or the system's default for "".
	bool Start( const std::string& device );
	void Stop();
	bool Running() const;
	const std::string& Status() const { return status; }

	// Main thread, once a frame: measures the latest sound. Each value is 0..1 (-60 dB to 0 dB after `gain`),
	// rising at once and falling back within a third of a second, as a VU meter does.
	void Analyse( double seconds );
	float Value( Band band ) const { return values[ band ]; }
	float gain = 1; // amplification before measuring (1: as it comes)

	// Sound handed in directly (mono), for tests: as if it had come from the device.
	void Feed( const float* samples, int frames, int sampleRate );

private:
	struct Device;
	std::unique_ptr< Device > device;
	std::string status;

	static constexpr int kWindow = 2048; // samples measured each time (about 43 ms at 48 kHz)
	std::mutex mutex;                    // the device's thread writes the ring, the main thread reads it
	std::vector< float > ring = std::vector< float >( kWindow, 0.0f );
	int write      = 0;
	int sampleRate = 48000;
	float values[ BandCount ] = {};
};
}
