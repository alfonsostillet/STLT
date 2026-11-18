#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include "nimslink/Net.h"
#include "nimslink/Protocol.h"

namespace nimslink
{
// The DJ's side: announces itself, answers pings and sends the decks to every receiver that subscribed.
class Sender
{
public:
	Sender() = default;
	~Sender();
	Sender( const Sender& )            = delete;
	Sender& operator=( const Sender& ) = delete;

	bool Start( const std::string& name, std::string& error, uint16_t port = kSenderPort );
	void Stop();
	bool Running() const { return running; }
	uint16_t Port() const { return socket.Port(); }
	uint32_t Id() const { return id; }

	// What feeds it: "Traktor Pro 4", or empty while nothing does.
	void SetProgram( const std::string& program );
	// A deck's track and hotcues (sent at once when they changed, then now and then).
	void SetTrack( int deck, const TrackInfo& info );
	// The decks as they were at `time` (Now()'s clock); sent to every subscriber at once.
	void Publish( const DeckState* decks, double time );
	// Also announce to this address (a receiver that broadcasts cannot reach; tests).
	void AnnounceTo( const Address& address );

	struct Subscriber
	{
		uint32_t id = 0;
		std::string name;
		Address address;
		float roundTrip = 0; // seconds, as the receiver measured it
		Visual visuals[ kDecks ] = {};
		double since    = 0;
		double heard    = 0; // when it last subscribed
	};
	std::vector< Subscriber > Subscribers() const;
	int PacketsSent() const { return sent; }

private:
	void Loop();
	void Announce( double now );
	void SendTrack( int deck, const Address& to );
	void SendAll( const std::vector< uint8_t >& bytes );

	UdpSocket socket;
	std::thread thread;
	std::atomic< bool > running{ false };
	std::atomic< int > sent{ 0 };
	uint32_t id = 0;
	std::string name;

	mutable std::mutex mutex; // what follows
	std::string program;
	std::vector< Subscriber > subscribers;
	TrackInfo tracks[ kDecks ];
	int urgent[ kDecks ] = {}; // sends of a changed track still to do
	std::vector< Address > extra;
	std::vector< Address > broadcasts;
	uint32_t seq  = 0;
	double listed = -100; // when the networks were last listed
};

// One transmitter as the receiver knows it.
struct Source
{
	uint32_t id = 0;
	std::string name;    // the DJ's computer
	std::string program; // "Traktor Pro 4"; empty: the transmitter runs but Traktor sends nothing
	Address address;
	bool online      = false; // heard within the last few seconds
	bool synced      = false; // the clocks are measured
	double roundTrip = 0;     // seconds
	int lost         = 0;     // deck packets that did not arrive
	int received     = 0;
	DeckNow decks[ kDecks ];
	TrackInfo tracks[ kDecks ];
	bool hasTrack[ kDecks ] = {}; // tracks[ i ] is the one deck i has loaded now
};

// The VJ's side: finds transmitters on the networks this computer is on (and at addresses given), subscribes
// and follows their decks.
class Receiver
{
public:
	Receiver() = default;
	~Receiver();
	Receiver( const Receiver& )            = delete;
	Receiver& operator=( const Receiver& ) = delete;

	bool Start( const std::string& name, std::string& error, uint16_t discoveryPort = kDiscoveryPort );
	void Stop();
	bool Running() const { return running; }
	// Transmitters to subscribe to even without hearing them ("192.168.1.14", "dj-laptop:52131").
	void SetAddresses( const std::vector< std::string >& hosts );
	// What this VJ has for a transmitter's decks (shown to the DJ); sent with the next subscription.
	void SetVisuals( uint32_t source, const Visual* visuals );
	// Every transmitter heard, in the order they were first heard, with their decks as they are at `now`.
	std::vector< Source > Sources( double now ) const;

private:
	struct Known
	{
		Source source;
		ClockSync clock;
		DeckTracker decks[ kDecks ];
		double lastHeard    = 0;
		double lastSubscribe = -10;
		double lastPing     = -10;
		double found        = 0;
		uint32_t lastSeq    = 0;
		Visual visuals[ kDecks ] = {};
		bool anySeq         = false;
	};
	void Loop();
	void Handle( const Packet& packet, const Address& from, double arrived );
	Known& Find( uint32_t id, const Address& from, double now );
	void Send( const Packet& packet, const Address& to );

	UdpSocket discovery, data;
	std::thread thread;
	std::atomic< bool > running{ false };
	uint32_t id = 0;
	std::string name;
	mutable std::mutex mutex;
	std::vector< Known > known;
	std::vector< Address > manual;
	std::vector< std::string > manualHosts;
	bool manualChanged = false;
	double manualAt    = -10;
};
}
