#pragma once
#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include "vjlink/Protocol.h"

namespace vjlink
{
// The transmitter's ear inside the DJ's computer: the QML script added to Traktor's mapping posts the decks, as
// JSON, to http://127.0.0.1:9125/traktor about 33 times a second (resources/vjlink/qml/TraktorVJLink.qml):
//   { v, session, seq, t, fast: [4 x [loaded, play, pos, rate, beatphase, beat, bpm, loopOn, loopStart, loopLen,
//                                     stem1, stem2, stem3, stem4, level, volume]],
//     info: [4 x { title, artist, key, file, entry, length, bpm, stems, hotcues: [8 x [type, start, length, name] | null] }] }
// with `info` every 15th post. Traktor can load the script twice (two sessions): one is followed.
class TraktorBridge
{
public:
	// The decks as they were when the post arrived (Now()'s clock); `tracks` with the posts that have them.
	using DecksHandler = std::function< void( const DeckState* decks, double time ) >;
	using TrackHandler = std::function< void( int deck, const TrackInfo& info ) >;

	TraktorBridge() = default;
	~TraktorBridge();
	TraktorBridge( const TraktorBridge& )            = delete;
	TraktorBridge& operator=( const TraktorBridge& ) = delete;

	bool Start( DecksHandler decks, TrackHandler tracks, std::string& error, uint16_t port = kTraktorPort );
	void Stop();
	bool Running() const { return running; }
	// Seconds since the last post (a large number when none came).
	double Silence() const;
	int Posts() const { return posts; }
	// Posts a second, measured over the last one.
	float Rate() const { return rate; }

	// One post's body: false when it is not one (tests use this directly).
	bool Parse( const std::string& body, double arrived );

private:
	void Loop();
	intptr_t listener = -1;
	std::thread thread;
	std::atomic< bool > running{ false };
	std::atomic< int > posts{ 0 };
	std::atomic< float > rate{ 0 };
	std::atomic< double > lastPost{ -1e9 };
	DecksHandler onDecks;
	TrackHandler onTrack;
	std::string session;
	double sessionSeen = -1e9;
	double rateStart   = 0;
	int rateCount      = 0;
};
}
