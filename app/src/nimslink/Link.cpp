#include "nimslink/Link.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <random>

namespace nimslink
{
namespace
{
uint32_t NewId()
{
	std::random_device device;
	uint32_t id = device() ^ (uint32_t)std::chrono::high_resolution_clock::now().time_since_epoch().count();
	return id ? id : 1;
}

bool SameTrackInfo( const TrackInfo& a, const TrackInfo& b )
{
	if( a.title != b.title || a.artist != b.artist || a.key != b.key || a.file != b.file || a.entry != b.entry ||
	    std::fabs( a.length - b.length ) > 1e-3 || std::fabs( a.bpm - b.bpm ) > 1e-3 || a.stems != b.stems )
		return false;
	for( int h = 0; h < kHotcues; ++h )
	{
		const Hotcue &x = a.hotcues[ h ], &y = b.hotcues[ h ];
		if( x.exists != y.exists || ( x.exists && ( x.type != y.type || std::fabs( x.start - y.start ) > 1e-4 ||
		                                            std::fabs( x.length - y.length ) > 1e-4 || x.name != y.name ) ) )
			return false;
	}
	return true;
}

// Seconds between announces, subscriptions and the resending of tracks; how long a silent peer is kept.
constexpr double kAnnounceEvery  = 1.0;
constexpr double kSubscribeEvery = 1.0;
constexpr double kForgetAfter    = 5.0;
constexpr double kOfflineAfter   = 3.0;
}

// ---- Sender

Sender::~Sender()
{
	Stop();
}

bool Sender::Start( const std::string& computer, std::string& error, uint16_t port )
{
	Stop();
	// The usual port, so a firewall rule made once keeps working; any free one when another program has it.
	if( !socket.Open( port, false, error ) && !socket.Open( 0, false, error ) )
		return false;
	id      = NewId();
	name    = computer;
	running = true;
	thread  = std::thread( [ this ] { Loop(); } );
	return true;
}

void Sender::Stop()
{
	if( !running )
		return;
	Packet bye;
	bye.type = PacketType::Bye;
	bye.from = id;
	SendAll( Encode( bye ) );
	running = false;
	if( thread.joinable() )
		thread.join();
	socket.Close();
	std::lock_guard< std::mutex > lock( mutex );
	subscribers.clear();
}

void Sender::SetProgram( const std::string& p )
{
	std::lock_guard< std::mutex > lock( mutex );
	program = p;
}

void Sender::AnnounceTo( const Address& address )
{
	std::lock_guard< std::mutex > lock( mutex );
	if( std::find( extra.begin(), extra.end(), address ) == extra.end() )
		extra.push_back( address );
}

void Sender::SetTrack( int deck, const TrackInfo& info )
{
	if( deck < 0 || deck >= kDecks )
		return;
	std::vector< Address > to;
	{
		std::lock_guard< std::mutex > lock( mutex );
		if( SameTrackInfo( tracks[ deck ], info ) )
			return;
		uint16_t version = (uint16_t)( tracks[ deck ].version + 1 );
		tracks[ deck ]   = info;
		tracks[ deck ].version = version ? version : 1;
		urgent[ deck ] = 3; // again in the next few rounds, in case one is lost
		for( const Subscriber& s : subscribers )
			to.push_back( s.address );
	}
	for( const Address& a : to )
		SendTrack( deck, a );
}

void Sender::SendTrack( int deck, const Address& to )
{
	Packet p;
	p.type = PacketType::Track;
	p.from = id;
	p.deck = deck;
	{
		std::lock_guard< std::mutex > lock( mutex );
		p.track = tracks[ deck ];
	}
	std::vector< uint8_t > bytes = Encode( p );
	if( socket.Send( to, bytes.data(), bytes.size() ) )
		++sent;
}

void Sender::SendAll( const std::vector< uint8_t >& bytes )
{
	std::vector< Address > to;
	{
		std::lock_guard< std::mutex > lock( mutex );
		for( const Subscriber& s : subscribers )
			to.push_back( s.address );
	}
	for( const Address& a : to )
		if( socket.Send( a, bytes.data(), bytes.size() ) )
			++sent;
}

void Sender::Publish( const DeckState* decks, double time )
{
	if( !running )
		return;
	Packet p;
	p.type = PacketType::Decks;
	p.from = id;
	p.time = time;
	{
		std::lock_guard< std::mutex > lock( mutex );
		p.seq = ++seq;
		for( int d = 0; d < kDecks; ++d )
		{
			p.decks[ d ]       = decks[ d ];
			p.decks[ d ].track = decks[ d ].loaded ? tracks[ d ].version : 0;
		}
	}
	SendAll( Encode( p ) );
}

std::vector< Sender::Subscriber > Sender::Subscribers() const
{
	std::lock_guard< std::mutex > lock( mutex );
	return subscribers;
}

void Sender::Announce( double now )
{
	Packet p;
	p.type = PacketType::Announce;
	p.from = id;
	p.port = socket.Port();
	p.name = name;
	std::vector< Address > to;
	{
		std::lock_guard< std::mutex > lock( mutex );
		p.program = program;
		if( now - listed > 5 || broadcasts.empty() )
		{
			// The networks again now and then: a cable plugged in, Wi-Fi joined.
			listed = now;
			broadcasts.clear();
			for( const Interface& i : Interfaces() )
			{
				Address a{ i.loopback ? 0x7F000001u : i.broadcast, kDiscoveryPort };
				if( a.ip && std::find( broadcasts.begin(), broadcasts.end(), a ) == broadcasts.end() )
					broadcasts.push_back( a );
			}
			Address all{ 0xFFFFFFFFu, kDiscoveryPort };
			if( std::find( broadcasts.begin(), broadcasts.end(), all ) == broadcasts.end() )
				broadcasts.push_back( all );
		}
		to = broadcasts;
		to.insert( to.end(), extra.begin(), extra.end() );
		for( const Subscriber& s : subscribers )
			to.push_back( s.address ); // receivers that came by address hear it too
	}
	std::vector< uint8_t > bytes = Encode( p );
	for( const Address& a : to )
		socket.Send( a, bytes.data(), bytes.size() );
}

void Sender::Loop()
{
	double announced = -10, resent = 0;
	int resendDeck   = 0;
	uint8_t buffer[ 2048 ];
	while( running )
	{
		double now = Now();
		if( now - announced >= kAnnounceEvery )
		{
			announced = now;
			Announce( now );
		}
		UdpSocket* s[] = { &socket };
		if( UdpSocket::Wait( s, 1, 0.02 ) == 0 )
		{
			Address from;
			int n;
			while( ( n = socket.Receive( buffer, sizeof( buffer ), from ) ) > 0 )
			{
				double arrived = Now();
				Packet in;
				if( !Decode( buffer, (size_t)n, in ) )
					continue;
				if( in.type == PacketType::Ping )
				{
					Packet out;
					out.type = PacketType::Pong;
					out.from = id;
					out.t0   = in.t0;
					out.t1   = arrived;
					out.t2   = Now();
					std::vector< uint8_t > bytes = Encode( out );
					socket.Send( from, bytes.data(), bytes.size() );
				}
				else if( in.type == PacketType::Subscribe )
				{
					bool added = false;
					{
						std::lock_guard< std::mutex > lock( mutex );
						auto it = std::find_if( subscribers.begin(), subscribers.end(), [ & ]( const Subscriber& x ) { return x.id == in.from; } );
						if( it == subscribers.end() )
						{
							Subscriber sub;
							sub.id    = in.from;
							sub.since = arrived;
							subscribers.push_back( sub );
							it    = subscribers.end() - 1;
							added = true;
						}
						it->name      = in.name;
						it->address   = from;
						it->roundTrip = in.roundTrip;
						for( int d = 0; d < kDecks; ++d )
							it->visuals[ d ] = (Visual)std::min< uint8_t >( in.visuals[ d ], 2 );
						it->heard     = arrived;
					}
					if( added )
						for( int d = 0; d < kDecks; ++d )
							SendTrack( d, from ); // a new receiver learns every track at once
				}
				else if( in.type == PacketType::Bye )
				{
					std::lock_guard< std::mutex > lock( mutex );
					subscribers.erase( std::remove_if( subscribers.begin(), subscribers.end(), [ & ]( const Subscriber& x ) { return x.id == in.from; } ),
					                   subscribers.end() );
				}
			}
		}
		now = Now();
		{
			std::lock_guard< std::mutex > lock( mutex );
			subscribers.erase( std::remove_if( subscribers.begin(), subscribers.end(), [ & ]( const Subscriber& x ) { return now - x.heard > kForgetAfter; } ),
			                   subscribers.end() );
		}
		// Tracks: a changed one in the next rounds, and one deck every quarter second whatever happens.
		for( int d = 0; d < kDecks; ++d )
		{
			bool due;
			{
				std::lock_guard< std::mutex > lock( mutex );
				due = urgent[ d ] > 0;
				if( due )
					--urgent[ d ];
			}
			if( due )
				for( const Subscriber& s : Subscribers() )
					SendTrack( d, s.address );
		}
		if( now - resent >= 0.25 )
		{
			resent = now;
			for( const Subscriber& s : Subscribers() )
				SendTrack( resendDeck, s.address );
			resendDeck = ( resendDeck + 1 ) % kDecks;
		}
	}
}

// ---- Receiver

Receiver::~Receiver()
{
	Stop();
}

bool Receiver::Start( const std::string& computer, std::string& error, uint16_t discoveryPort )
{
	Stop();
	if( !discovery.Open( discoveryPort, true, error ) )
		return false;
	if( !data.Open( 0, false, error ) )
	{
		discovery.Close();
		return false;
	}
	id      = NewId();
	name    = computer;
	running = true;
	thread  = std::thread( [ this ] { Loop(); } );
	return true;
}

void Receiver::Stop()
{
	if( !running )
		return;
	running = false;
	if( thread.joinable() )
		thread.join();
	Packet bye;
	bye.type = PacketType::Bye;
	bye.from = id;
	{
		std::lock_guard< std::mutex > lock( mutex );
		for( const Known& k : known )
			if( k.source.online )
				Send( bye, k.source.address );
		known.clear();
	}
	discovery.Close();
	data.Close();
}

void Receiver::SetAddresses( const std::vector< std::string >& hosts )
{
	std::lock_guard< std::mutex > lock( mutex );
	if( hosts == manualHosts )
		return;
	manualHosts   = hosts;
	manualChanged = true;
}

void Receiver::Send( const Packet& packet, const Address& to )
{
	std::vector< uint8_t > bytes = Encode( packet );
	data.Send( to, bytes.data(), bytes.size() );
}

Receiver::Known& Receiver::Find( uint32_t from, const Address& address, double now )
{
	for( Known& k : known )
		if( k.source.id == from )
			return k;
	// A transmitter restarted on the same computer comes back with another id: it takes the old one's place.
	for( Known& k : known )
		if( k.source.address == address )
		{
			k = Known();
			k.source.id      = from;
			k.source.address = address;
			k.found          = now;
			return k;
		}
	known.emplace_back();
	Known& k         = known.back();
	k.source.id      = from;
	k.source.address = address;
	k.found          = now;
	return k;
}

void Receiver::Handle( const Packet& p, const Address& from, double arrived )
{
	if( p.from == id )
		return;
	std::lock_guard< std::mutex > lock( mutex );
	if( p.type == PacketType::Bye )
	{
		for( Known& k : known )
			if( k.source.id == p.from )
				k.source.online = false;
		return;
	}
	if( p.type == PacketType::Subscribe || p.type == PacketType::Ping )
		return; // another receiver's
	// Answers go where the packet came from: the transmitter's socket (also for its broadcasts).
	Address at = from;
	Known& k        = Find( p.from, at, arrived );
	k.source.address = at;
	k.lastHeard     = arrived;
	k.source.online = true;
	switch( p.type )
	{
	case PacketType::Announce:
		k.source.name    = p.name;
		k.source.program = p.program;
		break;
	case PacketType::Pong:
		k.clock.Add( p.t0, p.t1, p.t2, arrived );
		k.source.synced    = k.clock.Ready();
		k.source.roundTrip = k.clock.RoundTrip();
		break;
	case PacketType::Decks:
	{
		if( k.anySeq && p.seq <= k.lastSeq && k.lastSeq - p.seq < 1000 )
			break; // late, after a newer one
		if( k.anySeq && p.seq > k.lastSeq + 1 )
			k.source.lost += (int)( p.seq - k.lastSeq - 1 );
		k.anySeq   = true;
		k.lastSeq  = p.seq;
		++k.source.received;
		// When the decks were as said, on this computer's clock (never after the packet arrived).
		double local = k.clock.Ready() ? std::min( k.clock.Local( p.time ), arrived ) : arrived;
		for( int d = 0; d < kDecks; ++d )
			k.decks[ d ].Add( p.decks[ d ], local );
		break;
	}
	case PacketType::Track:
		k.source.tracks[ p.deck ] = p.track;
		k.decks[ p.deck ].SetTrack( p.track );
		break;
	default: break;
	}
}

void Receiver::Loop()
{
	uint8_t buffer[ 2048 ];
	while( running )
	{
		UdpSocket* sockets[] = { &discovery, &data };
		int ready            = UdpSocket::Wait( sockets, 2, 0.01 );
		if( ready >= 0 )
			for( int s = 0; s < 2; ++s )
			{
				Address from;
				int n;
				while( ( n = sockets[ s ]->Receive( buffer, sizeof( buffer ), from ) ) > 0 )
				{
					double arrived = Now();
					Packet p;
					if( Decode( buffer, (size_t)n, p ) )
						Handle( p, from, arrived );
				}
			}
		double now = Now();
		std::vector< std::pair< Packet, Address > > out;
		{
			std::lock_guard< std::mutex > lock( mutex );
			if( manualChanged )
			{
				manualChanged = false;
				manual.clear();
				for( const std::string& h : manualHosts )
				{
					Address a;
					if( Address::Parse( h, kSenderPort, a ) )
						manual.push_back( a );
				}
			}
			for( Known& k : known )
			{
				if( k.source.online && now - k.lastHeard > kOfflineAfter )
					k.source.online = false;
				if( !k.source.online )
					continue;
				if( now - k.lastSubscribe >= kSubscribeEvery )
				{
					k.lastSubscribe = now;
					Packet sub;
					sub.type      = PacketType::Subscribe;
					sub.from      = id;
					sub.name      = name;
					sub.roundTrip = (float)k.clock.RoundTrip();
					for( int d = 0; d < kDecks; ++d )
						sub.visuals[ d ] = (uint8_t)k.visuals[ d ];
					out.emplace_back( sub, k.source.address );
				}
				// Pings: ten a second at first, to lock on quickly; then two.
				double every = now - k.found < 3 ? 0.1 : 0.5;
				if( now - k.lastPing >= every )
				{
					k.lastPing = now;
					Packet ping;
					ping.type = PacketType::Ping;
					ping.from = id;
					out.emplace_back( ping, k.source.address );
				}
			}
			// Addresses given: subscribed to every second until they answer (then they are known).
			if( now - manualAt >= kSubscribeEvery )
			{
				manualAt = now;
				for( const Address& a : manual )
				{
					bool online = false;
					for( const Known& k : known )
						online |= k.source.online && k.source.address.ip == a.ip;
					if( online )
						continue;
					Packet sub;
					sub.type = PacketType::Subscribe;
					sub.from = id;
					sub.name = name;
					out.emplace_back( sub, a );
				}
			}
			// Forgotten after a minute away.
			known.erase( std::remove_if( known.begin(), known.end(), [ & ]( const Known& k ) { return !k.source.online && now - k.lastHeard > 60; } ),
			             known.end() );
		}
		for( auto& [ packet, to ] : out )
		{
			if( packet.type == PacketType::Ping )
				packet.t0 = Now(); // as late as possible
			Send( packet, to );
		}
	}
}

void Receiver::SetVisuals( uint32_t source, const Visual* visuals )
{
	std::lock_guard< std::mutex > lock( mutex );
	for( Known& k : known )
		if( k.source.id == source && !std::equal( visuals, visuals + kDecks, k.visuals ) )
		{
			std::copy( visuals, visuals + kDecks, k.visuals );
			k.lastSubscribe = -10; // told at once
		}
}

std::vector< Source > Receiver::Sources( double now ) const
{
	std::lock_guard< std::mutex > lock( mutex );
	std::vector< Source > list;
	for( const Known& k : known )
	{
		Source s = k.source;
		for( int d = 0; d < kDecks; ++d )
		{
			s.decks[ d ] = k.decks[ d ].At( now );
			if( !s.online )
				s.decks[ d ].present = false;
			const TrackInfo* t = k.decks[ d ].Track();
			s.hasTrack[ d ]    = t != nullptr;
			if( t )
				s.tracks[ d ] = *t;
		}
		list.push_back( s );
	}
	return list;
}
}
