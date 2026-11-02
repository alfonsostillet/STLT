#pragma once
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>

namespace stltvj
{
// SMPTE timecode as the shows send it: LTC (audio), MTC (MIDI) and Art-Net. The decoders here take bytes and
// samples with their arrival time (seconds, any steady clock) and hand complete times to a TimecodeReceiver,
// which the timeline reads once a frame. docs/TIMECODE.md has the why and the how.

enum class TimecodeRate
{
	Fps24,
	Fps25,
	Fps2997Drop, // 29.97 with drop frame: the numbering skips frames 0 and 1 each minute but every tenth
	Fps30,
};
const char* TimecodeRateName( TimecodeRate rate );
double TimecodeRateFps( TimecodeRate rate ); // 24, 25, 29.97, 30
int TimecodeRateFrames( TimecodeRate rate ); // frames counted in a second: 24, 25, 30, 30

struct Timecode
{
	int hours = 0, minutes = 0, seconds = 0, frames = 0;
	TimecodeRate rate = TimecodeRate::Fps25;
	// Real seconds since 00:00:00:00 (drop frame counted out).
	double Seconds() const;
	std::string Text() const; // "HH:MM:SS:FF", with ';' before the frames in drop frame
};
// "HH:MM:SS:FF" (or with ';' or '.'); false when it is not one.
bool ParseTimecode( const std::string& text, Timecode& timecode );

// What the timeline follows: the latest time received and when, extrapolated while the timecode runs, and kept
// running for a while (freewheel) when it stops arriving, as a dropped cable or packet should not stop a show.
class TimecodeReceiver
{
public:
	// `running`: the time came as part of a running timecode (LTC words, MTC quarter frames, Art-Net packets),
	// not a single locate (MTC full frame); it still counts as stopped when it does not move.
	void Receive( const Timecode& timecode, double time, bool running = true );
	void Reset();

	struct State
	{
		bool present   = false; // something arrived within the freewheel (or the last few frames)
		bool running   = false;
		double seconds = 0;     // the timecode now, in real seconds
		Timecode last;          // the last time received
	};
	State At( double now, double freewheel ) const;

private:
	mutable std::mutex mutex;
	Timecode last;
	double lastSeconds = 0;
	double lastTime    = -1;
	double movedAt     = -1; // when it last moved on
	bool running       = false;
};

// LTC: the biphase-mark audio signal, 80 bits a frame ending in the sync word. Polarity and level do not matter;
// the tempo of the bits is learnt as it goes (24 to 30 fps at any sample rate). Only forward play is read.
class LtcDecoder
{
public:
	explicit LtcDecoder( TimecodeReceiver& receiver ) : receiver( receiver ) {}
	// `time`: when the last of the samples arrived.
	void Feed( const float* samples, size_t count, int sampleRate, double time, int stride = 1 );
	int Frames() const { return framesDecoded; }

private:
	void Bit( int bit, double time );
	TimecodeReceiver& receiver;
	int rate          = 0;
	double period     = 0;   // samples per bit, learnt
	double sinceEdge  = 0;   // samples since the last zero crossing
	bool high         = false;
	float envelope    = 0;
	bool halfPending  = false; // the first half of a 1
	std::array< uint8_t, 80 > bits{};
	int filled        = 0;
	double lastWordAt = -1;  // for the frame rate
	double wordPeriod = 0;   // seconds between words
	int64_t sample    = 0;
	std::atomic< int > framesDecoded{ 0 }; // read by the main thread for the status
};

// MTC: quarter frames (F1), eight to a time, and full frames (the locate SysEx).
class MtcDecoder
{
public:
	explicit MtcDecoder( TimecodeReceiver& receiver ) : receiver( receiver ) {}
	void Message( const unsigned char* bytes, size_t size, double time );

private:
	TimecodeReceiver& receiver;
	int pieces[ 8 ] = {};
	int seen        = 0;  // bit per piece received in order since piece 0
	int next        = 0;
};

// Art-Net's ArtTimeCode packet (OpCode 0x9700); false when the packet is not one.
bool ParseArtTimecode( const uint8_t* data, size_t size, Timecode& timecode );
}
