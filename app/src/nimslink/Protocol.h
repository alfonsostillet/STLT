#pragma once
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

// NIMS PRO LINK: the decks of a DJ program (Traktor, through a QML script added to a controller's mapping) sent over
// the network to a VJ program on another computer, as Pioneer's Pro DJ Link and Denon's StageLinQ do. The DJ's
// computer runs the transmitter; the VJ program has the receiver. docs/NIMSLINK.md has the why and the how.
//
// On the wire, UDP:
// - the transmitter announces itself every second, by broadcast to kDiscoveryPort on every network it is on;
// - a receiver that hears it subscribes (again every second, or it is forgotten after a few) and pings it to
//   measure the difference between the two clocks, as NTP does;
// - the transmitter sends the subscribers the four decks (33 times a second, as Traktor updates them) and, when a
//   deck loads another track, the track's details (and again now and then, as UDP can lose a packet).
// Data goes to each receiver on its own (unicast): Wi-Fi delivers broadcast late and at a low rate.
// Every value is little-endian; strings are a length byte and UTF-8.
namespace nimslink
{
constexpr uint16_t kDiscoveryPort = 52130; // receivers listen here for announces
constexpr uint16_t kSenderPort    = 52131; // the transmitter's socket (another one if taken)
constexpr uint16_t kTraktorPort   = 9125;  // the QML script inside Traktor posts to 127.0.0.1 here
constexpr uint8_t kVersion        = 1;
constexpr int kDecks              = 4;
constexpr int kStems              = 4;
constexpr int kHotcues            = 8;

enum class PacketType : uint8_t
{
	Announce  = 1, // transmitter -> broadcast: name, data port, whether Traktor sends
	Subscribe = 2, // receiver -> transmitter: keep sending me (with the receiver's name and measured round trip)
	Ping      = 3, // receiver -> transmitter: t0 (receiver clock)
	Pong      = 4, // transmitter -> receiver: t0, t1 (received), t2 (sent), transmitter clock
	Decks     = 5, // transmitter -> receiver: the four decks at one moment
	Track     = 6, // transmitter -> receiver: one deck's track and hotcues
	Bye       = 7, // either way: going away
};

// One deck at one moment, as Traktor reports it.
struct DeckState
{
	bool loaded     = false;
	bool playing    = false;
	bool inLoop     = false;
	uint16_t track  = 0;  // changes when the deck's track (or its hotcues) change: the Track packet with it
	double position = 0;  // seconds into the track
	float rate      = 1;  // playing speed, 1 = the track's own tempo
	float beatPhase = 0;  // 0..1 within the beat
	double beat     = 0;  // beats from the track's grid
	float bpm       = 0;  // as played (the track's BPM times the rate)
	double loopStart = 0, loopLength = 0; // seconds
	float stems[ kStems ] = {}; // the level of each stem (drums, bass, other, vocals), 0..1
	float level  = 0;           // the channel's level before the fader, 0..1
	float volume = 0;           // the channel's fader, 0..1
};

struct Hotcue
{
	bool exists   = false;
	int type      = -1; // Traktor's: -1 cue, 0 fade in, 1 fade out, 2 load, 3 grid, 4 AutoGrid, 5 loop
	double start  = 0;  // seconds
	double length = 0;  // seconds (loops)
	std::string name;
};

struct TrackInfo
{
	uint16_t version = 0; // DeckState::track
	std::string title, artist, key;
	std::string file;     // the path on the DJ's computer
	int64_t entry = 0;    // Traktor's collection entry
	double length = 0;    // seconds
	float bpm     = 0;    // the track's own BPM (its grid)
	bool stems    = false;
	Hotcue hotcues[ kHotcues ];
};

// A track as the VJ's side keeps it to recognise it again (a clip linked to a track): the file path is the DJ's
// computer's, so title, artist and length.
struct TrackId
{
	std::string title, artist;
	double length = 0;
	bool Empty() const { return title.empty() && artist.empty(); }
};
TrackId IdOf( const TrackInfo& info );
// Same title and artist (case and spaces aside) and lengths within a second (0: any length).
bool SameTrack( const TrackId& a, const TrackId& b );
// Title and artist as SameTrack compares them: two tracks with different keys are never the same (for sorting and
// matching many tracks at once).
std::string TrackKey( const TrackId& id );

// ---- Packets

// What the VJ's program has for a deck's track, shown on the DJ's side.
enum class Visual : uint8_t
{
	None  = 0, // nothing linked to it
	Ready = 1, // a clip is linked to the track
	OnAir = 2, // and it is playing
};

struct Packet
{
	PacketType type = PacketType::Bye;
	uint32_t from   = 0; // the sender's id: random, new each time a program starts
	// Announce, Subscribe
	std::string name;           // the computer's (and program's) name
	std::string program;        // Announce: "Traktor Pro 4" or empty while Traktor sends nothing
	uint16_t port      = 0;     // Announce: the transmitter's data port
	float roundTrip    = 0;     // Subscribe: seconds, as the receiver measured it (0: not yet)
	uint8_t visuals[ kDecks ] = {}; // Subscribe: what the VJ has for each deck's track (Visual), for the DJ to see
	// Ping, Pong
	double t0 = 0, t1 = 0, t2 = 0;
	// Decks
	uint32_t seq       = 0;
	double time        = 0;     // when the decks were as said, on the transmitter's clock (seconds)
	DeckState decks[ kDecks ];
	// Track
	int deck = 0;
	TrackInfo track;
};

std::vector< uint8_t > Encode( const Packet& packet );
// False when the bytes are not a packet of this version.
bool Decode( const uint8_t* data, size_t size, Packet& packet );

// ---- The difference between the transmitter's clock and the receiver's, from ping round trips (NTP's way):
// t0 sent, t1 received there, t2 answered there, t3 received here. Round trips that took longest are the ones
// queued somewhere, and are left out; a line through the good ones follows the clocks drifting apart.
class ClockSync
{
public:
	void Add( double t0, double t1, double t2, double t3 );
	void Reset();
	bool Ready() const { return ready; }
	// The transmitter's time for a local time, and back.
	double Remote( double local ) const;
	double Local( double remote ) const;
	double RoundTrip() const { return roundTrip; } // the best recent one, seconds
	int Samples() const { return (int)samples.size(); }

private:
	struct Sample
	{
		double local, offset, delay;
	};
	std::deque< Sample > samples;
	bool ready       = false;
	double offset    = 0; // remote - local at `reference`
	double drift     = 0; // seconds per second
	double reference = 0;
	double roundTrip = 0;
};

// ---- A deck followed between packets: where it is now, from the last moments Traktor reported.
//
// Traktor's script reads the deck about 33 times a second and the values it reads can be up to one update old,
// so each sample is late by a varying amount. While the deck plays steadily, the samples lie on one line
// (position = rate x time + constant) and the least late of them are the ones furthest ahead on it: the line is
// drawn through those, which takes most of the jitter out. A jump (cue, hotcue, beatjump, a loop going round)
// or a change of speed starts the line again.
struct DeckNow
{
	bool present = false; // the transmitter sent this deck recently
	DeckState state;      // the last one received (stems, levels, loop...)
	double position = 0;  // seconds into the track, now
	double beat     = 0;  // beats from the grid, now
	double bpm      = 0;
	// Counters that change when something happened, and what it was.
	int jumps         = 0;
	double jumpFrom   = 0, jumpTo = 0;
	int jumpHotcue    = 0; // the hotcue (1..8) jumped to, 0 none
	bool jumpWasLoop  = false;
	int starts        = 0; // times the deck went from pause to play
	double age        = 0; // seconds since the last sample
};

class DeckTracker
{
public:
	// A sample, with the local time at which the deck was as said.
	void Add( const DeckState& state, double local );
	void SetTrack( const TrackInfo& info ) { track = info; hasTrack = true; }
	void Reset();
	const TrackInfo* Track() const { return hasTrack && state.loaded && track.version == state.track ? &track : nullptr; }
	DeckNow At( double now ) const;
	// Seconds a jump must be off the line (smaller ones are corrected softly).
	static constexpr double kJumpThreshold = 0.15;

private:
	double Line( double local ) const; // the position the line gives at a local time
	DeckState state;
	TrackInfo track;
	bool hasTrack  = false;
	bool any       = false;
	double last    = 0;     // the local time of the last sample
	// The line: position = lineRate x (local - lineTime) + linePosition, from the samples since it started.
	std::deque< std::pair< double, double > > window; // (local, position) since the line started, the last second
	double lineTime = 0, linePosition = 0, lineRate = 1;
	int jumps = 0, starts = 0, jumpHotcue = 0;
	double jumpFrom = 0, jumpTo = 0;
	bool jumpWasLoop = false;
	double beatOffset = 0; // beat - position x bpm / 60 of the last sample
};

// The deck whose music is on: playing, with the highest fader (another one takes over only when clearly higher).
// -1 when none plays. `current` is the one chosen last time.
int LeadingDeck( const DeckNow* decks, int count, int current );
}
