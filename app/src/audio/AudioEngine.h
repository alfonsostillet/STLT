#pragma once
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "media/AudioDecoder.h"

namespace stltvj
{
// A clip's sound while it plays. It is decoded a little ahead on its own thread into a short buffer that the
// audio device drains; the main loop tells it every frame where the clip is, how fast it runs and how loud it
// is, and it jumps when the clip jumps (loops, cue points, scrubbing).
class AudioVoice
{
public:
	// Null when the file has no sound.
	static std::shared_ptr< AudioVoice > Open( const std::string& path, int sampleRate );
	~AudioVoice();

	// Main loop. `rate` is the clip's speed (1: as recorded, pitch follows as on a turntable; 0 or backwards:
	// silent); `gain` its loudness.
	void Follow( double seconds, double rate, float gain );
	// Audio thread: adds this voice to `out` (stereo frames).
	void Mix( float* out, int frames );
	// The clip time being heard.
	double Playhead() const;
	// Seconds decoded ahead of it.
	double Buffered() const;

private:
	AudioVoice() = default;
	void Run();
	double PlayheadLocked() const;

	AudioDecoder decoder;
	int rate = 48000;
	std::thread worker;
	mutable std::mutex mutex;
	std::condition_variable wake;
	bool quit = false;

	// Guarded by the mutex.
	std::vector< float > ring; // stereo frames
	size_t head = 0, count = 0;
	double bufferStart = 0;    // clip time of the frame at `head`
	double fraction    = 0;    // between `head` and the next frame, when resampling
	int generation     = 0;    // bumps on every jump
	double seekTo      = -1;
	bool started       = false;
	double speed       = 0;
	float gain = 0, lastGain = 0;
};

// The audio device and the mix of the voices that play.
class AudioEngine
{
public:
	AudioEngine();
	~AudioEngine();
	// Opens the system's default output; without one, the engine stays silent and says why.
	bool Start();
	// For tests: runs without a device at `sampleRate` (Render is then called by hand).
	void StartHeadless( int sampleRate );
	void Stop();

	int SampleRate() const { return sampleRate; }
	std::string Status() const { return status; }

	// Main loop: the voices to hear now, and the master volume.
	void SetVoices( std::vector< std::shared_ptr< AudioVoice > > voices, float master );
	// Audio thread: the next `frames` stereo frames.
	void Render( float* out, int frames );

private:
	struct Device;
	std::unique_ptr< Device > device;
	int sampleRate = 0;
	std::string status = "off";
	std::mutex mutex;
	std::vector< std::shared_ptr< AudioVoice > > voices, playing; // playing: the audio thread's copy
	// Lists replaced lately, kept so that a voice is never freed (its thread joined) on the audio thread.
	std::vector< std::shared_ptr< AudioVoice > > retired[ 2 ];
	float master = 1, lastMaster = 1;
};
}
