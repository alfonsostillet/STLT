#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace stltvj
{
// A MIDI clock coming in (24 ticks a beat, with Start, Continue, Stop and Song Position), as a tempo and a beat.
// Messages come from the MIDI threads with their arrival time in seconds (any steady clock); the main thread
// reads At() once a frame.
class MidiClock
{
public:
	void Message( const unsigned char* bytes, size_t size, double time );

	struct State
	{
		bool receiving = false;    // ticks are arriving
		bool playing   = false;    // the sender's transport runs (or ticks came with no Start: taken as running)
		bool placed    = false;    // the beat is the sender's (after Start or Song Position), not counted from joining
		double bpm     = 0;        // 0 until enough ticks
		double beat    = 0;        // beats since Start (or since joining), between ticks as time goes
		int jumps      = 0;        // changes each time the position is set (Start, Song Position)
	};
	State At( double now ) const;
	void Reset();

private:
	mutable std::mutex mutex;
	std::vector< double > ticks; // recent tick times, for the tempo
	double lastTick = -1;
	int64_t count   = 0;         // ticks of position since Start or the Song Position
	bool playing    = false;
	bool started    = false;     // a Start, Continue or Stop has been seen
	bool placed     = false;
	int jumps       = 0;
	int pendingSpp  = -1;        // a Song Position waiting for Continue
	mutable double lastBpm = 0;  // the last tempo measured
};

// Where the tempo and the beat come from: the composition's own BPM (tap, typed, mapped), Ableton Link, or a
// MIDI clock. Link and the clock follow the music whatever the composition's speed or pause.
class TempoSync
{
public:
	TempoSync();
	~TempoSync();
	TempoSync( const TempoSync& ) = delete;
	TempoSync& operator=( const TempoSync& ) = delete;

	static double Now();       // seconds on the steady clock, as the MIDI threads stamp messages
	static bool LinkBuiltIn(); // this build has Ableton Link
	void EnableLink( bool on, double bpm, double beat );
	bool LinkEnabled() const;
	int LinkPeers() const;

	// The MIDI input whose clock is followed: "" none, "*" any.
	void SetClockInput( const std::string& input );
	std::string ClockInput() const;
	MidiClock clock;
	// From the MIDI threads: every system message (0xF0 and up) with the input it came from.
	void OnMidi( const std::string& device, const unsigned char* bytes, size_t size, double time );

	// The picture shows later than it is drawn (the projector, the output's frames): the beat is taken this far
	// ahead, in seconds.
	double latency = 0;

	enum class Source
	{
		Internal,
		Link,
		Clock
	};
	// Once a frame, after the composition's own beat moved: `bpm` and `beat` follow the source. A BPM the user
	// changed since the last call goes to Link (the clock ignores it). True when the beat jumped back (a Start,
	// a Song Position, a new Link timeline): what waited for a beat should go now.
	bool Update( double now, double& bpm, double& beat );
	Source Following() const { return following; }
	// Resync: beat 0 is now. With Link this is asked of the session (alone it is at once; with others, on the
	// next bar).
	void Resync( double now, double& beat );
	std::string Status() const;

private:
	mutable std::mutex inputMutex; // the MIDI threads read the input's name
	std::string clockInput;
	struct Link;
	std::unique_ptr< Link > link;
	Source following = Source::Internal;
	double sentBpm   = 0; // the BPM as of the last Update: a different one was changed by the user
	int clockJumps   = -1;
	double clockBase = 0; // added to the clock's beat when it gave no position
	bool clockWasPlaying = false;
	MidiClock::State lastClock;
};
}
