// Tests for VJ LINK: packets, the clock difference, decks followed between packets, Traktor's posts, and a
// transmitter and a receiver on this computer, straight and through a network that delays, jitters and loses.
// The measurements are printed: they are the numbers to compare with the real thing on two computers.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <random>
#include <thread>
#include <vector>
#include "vjlink/Link.h"
#include "vjlink/Net.h"
#include "vjlink/Protocol.h"
#include "vjlink/TraktorBridge.h"
#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using namespace vjlink;

static int failures = 0;

#define CHECK( condition )                                                                           \
	do                                                                                               \
	{                                                                                                \
		if( !( condition ) )                                                                         \
		{                                                                                            \
			std::fprintf( stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition ); \
			++failures;                                                                              \
		}                                                                                            \
	} while( 0 )

static void Sleep( double seconds )
{
	std::this_thread::sleep_for( std::chrono::duration< double >( seconds ) );
}

static TrackInfo Kerala()
{
	TrackInfo t;
	t.title  = "Kerala";
	t.artist = "Bonobo";
	t.key    = "11m";
	t.file   = "C:\\Music\\Bonobo - Kerala.mp3";
	t.entry  = 42;
	t.length = 241.5;
	t.bpm    = 62.5;
	t.stems  = true;
	t.hotcues[ 0 ] = { true, -1, 10.0, 0, "Intro" };
	t.hotcues[ 3 ] = { true, 5, 51.2, 7.68, "Loop" };
	return t;
}

static void TestPackets()
{
	Packet a;
	a.type    = PacketType::Announce;
	a.from    = 0xDEADBEEF;
	a.port    = 52131;
	a.name    = "DJ-LAPTOP";
	a.program = "Traktor Pro 4";
	std::vector< uint8_t > bytes = Encode( a );
	Packet b;
	CHECK( Decode( bytes.data(), bytes.size(), b ) && b.type == PacketType::Announce && b.from == 0xDEADBEEF && b.port == 52131 &&
	       b.name == "DJ-LAPTOP" && b.program == "Traktor Pro 4" );

	Packet d;
	d.type = PacketType::Decks;
	d.seq  = 77;
	d.time = 12345.678901234;
	d.decks[ 1 ].loaded   = true;
	d.decks[ 1 ].playing  = true;
	d.decks[ 1 ].inLoop   = true;
	d.decks[ 1 ].track    = 3;
	d.decks[ 1 ].position = 61.25;
	d.decks[ 1 ].rate     = 1.02f;
	d.decks[ 1 ].beat     = 63.8;
	d.decks[ 1 ].bpm      = 127.5f;
	d.decks[ 1 ].stems[ 2 ] = 0.64f;
	d.decks[ 1 ].volume   = 0.9f;
	bytes = Encode( d );
	CHECK( bytes.size() < 400 ); // one small packet, 33 times a second
	CHECK( Decode( bytes.data(), bytes.size(), b ) && b.type == PacketType::Decks && b.seq == 77 && b.time == 12345.678901234 );
	CHECK( b.decks[ 1 ].loaded && b.decks[ 1 ].playing && b.decks[ 1 ].inLoop && b.decks[ 1 ].track == 3 && b.decks[ 1 ].position == 61.25 );
	CHECK( b.decks[ 1 ].rate == 1.02f && b.decks[ 1 ].stems[ 2 ] == 0.64f && b.decks[ 1 ].volume == 0.9f && !b.decks[ 0 ].loaded );

	Packet t;
	t.type  = PacketType::Track;
	t.deck  = 2;
	t.track = Kerala();
	t.track.version = 9;
	bytes = Encode( t );
	CHECK( Decode( bytes.data(), bytes.size(), b ) && b.deck == 2 && b.track.title == "Kerala" && b.track.artist == "Bonobo" &&
	       b.track.version == 9 && b.track.length == 241.5 && b.track.bpm == 62.5f && b.track.stems && b.track.entry == 42 );
	CHECK( b.track.hotcues[ 0 ].exists && b.track.hotcues[ 0 ].start == 10.0 && b.track.hotcues[ 0 ].name == "Intro" );
	CHECK( !b.track.hotcues[ 1 ].exists && b.track.hotcues[ 3 ].type == 5 && b.track.hotcues[ 3 ].length == 7.68 );

	Packet p;
	p.type = PacketType::Pong;
	p.t0   = 1.5;
	p.t1   = 2.25;
	p.t2   = 2.5;
	bytes  = Encode( p );
	CHECK( Decode( bytes.data(), bytes.size(), b ) && b.t0 == 1.5 && b.t1 == 2.25 && b.t2 == 2.5 );

	// Not ours, cut short, another version.
	const char junk[] = "hello world, not a packet";
	CHECK( !Decode( (const uint8_t*)junk, sizeof( junk ), b ) );
	bytes = Encode( t );
	CHECK( !Decode( bytes.data(), bytes.size() - 3, b ) );
	bytes[ 4 ] = 99;
	CHECK( !Decode( bytes.data(), bytes.size(), b ) );
	// A long name is cut at 255 bytes, never in the middle of a character.
	Packet longName;
	longName.type = PacketType::Announce;
	for( int i = 0; i < 200; ++i )
		longName.name += "è";
	bytes = Encode( longName );
	CHECK( Decode( bytes.data(), bytes.size(), b ) && b.name.size() == 254 && b.name.substr( 0, 2 ) == "è" );
}

static void TestTrackIds()
{
	TrackInfo k = Kerala();
	TrackId a   = IdOf( k );
	TrackId b   = a;
	b.title     = "  KERALA ";
	b.length    = 241.9;
	CHECK( SameTrack( a, b ) );
	b.length = 250;
	CHECK( !SameTrack( a, b ) );
	b.length = 0; // a link made without a length matches any
	CHECK( SameTrack( a, b ) );
	b.artist = "Someone else";
	CHECK( !SameTrack( a, b ) );
	TrackInfo untagged;
	untagged.file = "/Users/dj/Music/03 Cirrus.aiff";
	CHECK( IdOf( untagged ).title == "03 Cirrus" );
	CHECK( !SameTrack( TrackId(), TrackId() ) );
}

// The transmitter's clock runs 20 parts per million fast and 1234.5 s ahead; round trips take 0.2..0.6 ms, and one
// in five waits in a queue for up to 20 ms (one way or the other).
static void TestClock()
{
	std::mt19937 random( 7 );
	std::uniform_real_distribution< double > base( 0.0001, 0.0003 ), queue( 0, 0.02 ), coin( 0, 1 );
	auto remote = []( double local ) { return local * ( 1 + 20e-6 ) + 1234.5; };
	ClockSync clock;
	CHECK( !clock.Ready() );
	double worst = 0;
	for( int i = 0; i < 240; ++i ) // two minutes, a ping every half second
	{
		double t0 = 100 + i * 0.5;
		double go = base( random ) + ( coin( random ) < 0.2 ? queue( random ) : 0 );
		double back = base( random ) + ( coin( random ) < 0.2 ? queue( random ) : 0 );
		double t1 = remote( t0 + go ), t2 = remote( t0 + go + 0.00005 );
		double t3 = t0 + go + 0.00005 + back;
		clock.Add( t0, t1, t2, t3 );
		if( i > 10 )
			worst = std::max( worst, std::fabs( clock.Remote( t3 ) - remote( t3 ) ) );
	}
	CHECK( clock.Ready() );
	double now = 100 + 240 * 0.5;
	std::printf( "clock: worst error %.3f ms over two minutes, best round trip %.3f ms\n", worst * 1000, clock.RoundTrip() * 1000 );
	CHECK( worst < 0.0002 );
	CHECK( std::fabs( clock.Local( remote( now ) ) - now ) < 0.0002 );
	CHECK( clock.RoundTrip() < 0.0007 );
	// An answer to a ping long gone, or a clock gone back, is left out.
	ClockSync other;
	other.Add( 10, 5, 5, 9 );
	CHECK( !other.Ready() );
}

// A deck as Traktor's script reads it: 33 times a second, each value up to one update (30 ms) old.
struct FakeDeck
{
	double origin = 0, start = 30, rate = 1;
	bool playing  = true;
	double Truth( double t ) const { return playing ? start + rate * ( t - origin ) : start; }
	void JumpTo( double t, double position )
	{
		start  = position;
		origin = t;
	}
};

static void TestDeck()
{
	std::mt19937 random( 3 );
	std::uniform_real_distribution< double > stale( 0, 0.030 ), wire( 0.0002, 0.0015 );
	TrackInfo track = Kerala();
	track.version   = 1;
	DeckTracker deck;
	deck.SetTrack( track );
	FakeDeck fake;
	DeckState s;
	s.loaded = true;
	s.track  = 1;
	s.bpm    = 62.5f;
	double sum = 0, worst = 0;
	int count = 0;
	auto feed = [ & ]( double t ) {
		double read = t - stale( random ); // the value Traktor's script read was as of then
		s.playing   = fake.playing;
		s.rate      = (float)fake.rate;
		s.position  = fake.Truth( read );
		s.beat      = s.position * 62.5 / 60 + 0.25;
		deck.Add( s, t + wire( random ) ); // stamped when it reached the transmitter
	};
	double t = 0;
	for( int i = 0; i < 300; ++i, t += 0.030 ) // nine seconds
	{
		feed( t );
		for( double f = 0; f < 0.030; f += 1.0 / 60 ) // the picture asks 60 times a second
		{
			DeckNow now = deck.At( t + f );
			double error = std::fabs( now.position - fake.Truth( t + f ) );
			if( i > 40 )
			{
				sum += error;
				worst = std::max( worst, error );
				++count;
			}
		}
	}
	std::printf( "deck: position error %.2f ms on average, %.2f ms at worst (samples up to 30 ms late)\n", sum / count * 1000, worst * 1000 );
	CHECK( sum / count < 0.004 );
	CHECK( worst < 0.010 );
	DeckNow now = deck.At( t );
	CHECK( now.present && now.starts == 1 && now.jumps == 0 );
	CHECK( std::fabs( now.beat - ( now.position * 62.5 / 60 + 0.25 ) ) < 1e-6 );
	CHECK( deck.Track() && deck.Track()->title == "Kerala" );

	// Hotcue 4: a jump, recognised, followed within one packet.
	fake.JumpTo( t, 51.2 );
	feed( t );
	now = deck.At( t + 0.001 );
	CHECK( now.jumps == 1 && now.jumpHotcue == 4 && std::fabs( now.jumpTo - 51.2 ) < 0.04 && std::fabs( now.position - 51.2 ) < 0.04 );
	// A loop going round: a jump back to the loop's start.
	for( int i = 0; i < 10; ++i )
		feed( t += 0.030 );
	s.inLoop     = true;
	s.loopStart  = 40.0;
	s.loopLength = 4.0;
	fake.JumpTo( t, 40.0 );
	feed( t );
	now = deck.At( t );
	CHECK( now.jumps == 2 && now.jumpWasLoop && !now.jumpHotcue );
	s.inLoop = false;
	// Pause: it stays where Traktor says.
	for( int i = 0; i < 10; ++i )
		feed( t += 0.030 );
	fake.start   = fake.Truth( t );
	fake.playing = false;
	feed( t );
	double held = deck.At( t ).position;
	CHECK( std::fabs( deck.At( t + 0.5 ).position - held ) < 1e-9 );
	// Play again: counted, and a speed change (pitch fader) is followed at once.
	fake.playing = true;
	fake.origin  = t;
	fake.rate    = 1.06;
	feed( t += 0.030 );
	feed( t += 0.030 );
	now = deck.At( t + 0.015 );
	CHECK( now.starts == 2 && now.jumps == 2 );
	CHECK( std::fabs( now.position - fake.Truth( t + 0.015 ) ) < 0.035 );
	// Without news the deck stops on the line after two seconds (the transmitter went away).
	double stop = deck.At( t + 3.0 ).position;
	CHECK( std::fabs( deck.At( t + 10.0 ).position - stop ) < 1e-9 );
	// Another track loaded: no jump counted.
	s.track    = 2;
	s.position = 0;
	deck.Add( s, t + 0.03 );
	CHECK( deck.At( t + 0.03 ).jumps == 2 && !deck.Track() );
}

static void TestLeadingDeck()
{
	DeckNow d[ 4 ];
	CHECK( LeadingDeck( d, 4, -1 ) == -1 );
	for( int i = 0; i < 2; ++i )
	{
		d[ i ].present       = true;
		d[ i ].state.loaded  = true;
		d[ i ].state.playing = true;
	}
	d[ 0 ].state.volume = 1.0f;
	d[ 1 ].state.volume = 0.2f;
	CHECK( LeadingDeck( d, 4, -1 ) == 0 );
	d[ 1 ].state.volume = 1.0f; // the DJ brings B in: A stays until B is clearly louder
	d[ 0 ].state.volume = 0.95f;
	CHECK( LeadingDeck( d, 4, 0 ) == 0 );
	d[ 0 ].state.volume = 0.6f;
	CHECK( LeadingDeck( d, 4, 0 ) == 1 );
	d[ 1 ].state.playing = false;
	CHECK( LeadingDeck( d, 4, 1 ) == 0 );
}

static const char* kPost = R"({"v":1,"session":"s1","seq":0,"t":1700000000000,
 "fast":[[1,1,61.5,1.0,0.25,64.06,62.5,0,0,0,0.59,0.0009,0.64,0.0008,0.7,1],
         [1,0,0,1,0,0,125,0,0,0,0,0,0,0,0,0],[0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0],[0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0]],
 "info":[{"title":"Kerala","artist":"Bonobo","key":"11m","file":"C:\\Music\\Kerala.mp3","entry":42,"length":241.5,"bpm":62.5,"stems":true,
          "hotcues":[[-1,10,0,"Intro"],null,null,[5,51.2,7.68,"Loop"],null,null,null,null]},
         {"title":"Kong","artist":"Bonobo","key":"8m","file":"","entry":7,"length":232,"bpm":125,"stems":false,"hotcues":[]},
         {"title":"","artist":"","key":"","file":"","entry":0,"length":0,"bpm":0,"stems":false,"hotcues":[]},
         {"title":"","artist":"","key":"","file":"","entry":0,"length":0,"bpm":0,"stems":false,"hotcues":[]}]})";

static void TestTraktorPosts()
{
	TraktorBridge bridge;
	DeckState got[ kDecks ];
	TrackInfo tracks[ kDecks ];
	int decksCalls = 0;
	std::string error;
	// Parse alone, without the server.
	struct Capture
	{
		DeckState* got;
		int* calls;
	} capture{ got, &decksCalls };
	(void)capture;
	TraktorBridge::DecksHandler onDecks = [ & ]( const DeckState* d, double ) {
		std::copy( d, d + kDecks, got );
		++decksCalls;
	};
	TraktorBridge::TrackHandler onTrack = [ & ]( int deck, const TrackInfo& t ) { tracks[ deck ] = t; };
	// The server on a test port, and a post as Traktor's script makes it.
	CHECK( bridge.Start( onDecks, onTrack, error, 19125 ) );
	std::string body    = kPost;
	std::string request = "POST /traktor HTTP/1.1\r\nHost: 127.0.0.1:19125\r\nContent-Type: application/json\r\nContent-Length: " +
	                      std::to_string( body.size() ) + "\r\n\r\n" + body;
	auto post = [ & ]( const std::string& text ) {
		int s = (int)socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
		sockaddr_in a{};
		a.sin_family      = AF_INET;
		a.sin_addr.s_addr = htonl( 0x7F000001 );
		a.sin_port        = htons( 19125 );
		std::string reply;
		if( connect( s, (sockaddr*)&a, sizeof( a ) ) == 0 )
		{
			send( s, text.data(), (int)text.size(), 0 );
			char buffer[ 512 ];
			int n;
			while( ( n = (int)recv( s, buffer, sizeof( buffer ), 0 ) ) > 0 )
				reply.append( buffer, (size_t)n );
		}
#ifdef _WIN32
		closesocket( s );
#else
		close( s );
#endif
		return reply;
	};
	std::string reply = post( request );
	CHECK( reply.find( "204" ) != std::string::npos && reply.find( "Connection: close" ) != std::string::npos );
	Sleep( 0.05 );
	CHECK( decksCalls == 1 && bridge.Posts() == 1 && bridge.Silence() < 1 );
	CHECK( got[ 0 ].loaded && got[ 0 ].playing && got[ 0 ].position == 61.5 && got[ 0 ].bpm == 62.5f && std::fabs( got[ 0 ].stems[ 2 ] - 0.64f ) < 1e-6 );
	CHECK( got[ 1 ].loaded && !got[ 1 ].playing && !got[ 2 ].loaded );
	CHECK( tracks[ 0 ].title == "Kerala" && tracks[ 0 ].hotcues[ 3 ].exists && tracks[ 0 ].hotcues[ 3 ].type == 5 && tracks[ 0 ].hotcues[ 3 ].length == 7.68 );
	CHECK( tracks[ 0 ].hotcues[ 0 ].name == "Intro" && !tracks[ 0 ].hotcues[ 1 ].exists && tracks[ 1 ].title == "Kong" );
	// A GET is the status page.
	reply = post( "GET / HTTP/1.1\r\nHost: x\r\n\r\n" );
	CHECK( reply.find( "200 OK" ) != std::string::npos && reply.find( "1 posts" ) != std::string::npos );
	// A second session (the script loaded twice) is left out while the first one posts.
	std::string other = body;
	other.replace( other.find( "\"s1\"" ), 4, "\"s2\"" );
	CHECK( bridge.Parse( other, Now() ) );
	CHECK( decksCalls == 1 );
	CHECK( !bridge.Parse( "not json", Now() ) );
	bridge.Stop();
	// The port is free again.
	CHECK( bridge.Start( onDecks, onTrack, error, 19125 ) );
	bridge.Stop();
}

// A network between the two: each packet waits 1..`jitter` ms more (so some overtake others), and `loss` of them
// are lost. It sits at an address of its own: the receiver is given it, the transmitter answers to it.
class BadNetwork
{
public:
	BadNetwork( uint16_t target, double jitter, double loss ) : target{ 0x7F000001, target }, jitter( jitter ), loss( loss )
	{
		std::string error;
		front.Open( 0, false, error ); // the receiver's side
		back.Open( 0, false, error );  // the transmitter's side
		thread = std::thread( [ this ] { Loop(); } );
	}
	~BadNetwork()
	{
		running = false;
		thread.join();
	}
	uint16_t Port() const { return front.Port(); }

private:
	struct Held
	{
		double at;
		bool toTransmitter;
		std::vector< uint8_t > bytes;
	};
	void Loop()
	{
		std::mt19937 random( 11 );
		std::uniform_real_distribution< double > delay( 0.001, jitter ), coin( 0, 1 );
		std::vector< Held > held;
		uint8_t buffer[ 2048 ];
		while( running )
		{
			UdpSocket* s[] = { &front, &back };
			UdpSocket::Wait( s, 2, 0.0005 );
			double now = Now();
			for( int i = 0; i < 2; ++i )
			{
				Address from;
				int n;
				while( ( n = s[ i ]->Receive( buffer, sizeof( buffer ), from ) ) > 0 )
				{
					if( i == 0 )
						receiver = from;
					if( coin( random ) < loss )
						continue;
					held.push_back( { now + delay( random ), i == 0, std::vector< uint8_t >( buffer, buffer + n ) } );
				}
			}
			for( size_t k = 0; k < held.size(); )
				if( held[ k ].at <= now )
				{
					if( held[ k ].toTransmitter )
						back.Send( target, held[ k ].bytes.data(), held[ k ].bytes.size() );
					else if( receiver.port )
						front.Send( receiver, held[ k ].bytes.data(), held[ k ].bytes.size() );
					held.erase( held.begin() + (ptrdiff_t)k );
				}
				else
					++k;
		}
	}
	UdpSocket front, back;
	Address target, receiver;
	double jitter, loss;
	std::atomic< bool > running{ true };
	std::thread thread;
};

// Traktor in a thread: deck A plays Kerala, its posts up to 30 ms stale, 33 a second, into a transmitter.
struct FakeTraktor
{
	Sender& sender;
	std::atomic< bool > running{ true };
	std::atomic< double > origin{ 0 };
	std::atomic< double > jumpAt{ -1 }; // a jump to hotcue 1 at this time
	std::thread thread;
	explicit FakeTraktor( Sender& to ) : sender( to )
	{
		origin = Now();
		TrackInfo k = Kerala();
		to.SetTrack( 0, k );
		to.SetProgram( "Traktor Pro 4" );
		thread = std::thread( [ this ] {
			std::mt19937 random( 5 );
			std::uniform_real_distribution< double > stale( 0, 0.030 );
			while( running )
			{
				double now = Now();
				DeckState d[ kDecks ];
				d[ 0 ].loaded   = true;
				d[ 0 ].playing  = true;
				d[ 0 ].bpm      = 62.5f;
				d[ 0 ].position = Truth( now - stale( random ) );
				d[ 0 ].beat     = d[ 0 ].position * 62.5 / 60;
				d[ 0 ].volume   = 1;
				d[ 0 ].stems[ 0 ] = 0.5f;
				sender.Publish( d, now );
				Sleep( 0.030 );
			}
		} );
	}
	~FakeTraktor()
	{
		running = false;
		thread.join();
	}
	double Truth( double t ) const
	{
		double j = jumpAt;
		if( j > 0 && t >= j )
			return 10.0 + ( t - j ); // hotcue 1
		return 30.0 + ( t - origin );
	}
};

struct Measured
{
	double mean = 0, worst = 0, followJump = -1;
	bool found = false, synced = false, track = false;
	double roundTrip = 0;
	int lost = 0;
	std::string name;
};

// Watches deck A at the receiver 60 times a second for `seconds`, against the truth.
static Measured Watch( Receiver& receiver, FakeTraktor& traktor, double seconds, bool jump )
{
	Measured m;
	double sum = 0;
	int count  = 0;
	double start = Now();
	// Locking on first: found, clocks measured.
	while( Now() - start < 5 )
	{
		std::vector< Source > s = receiver.Sources( Now() );
		if( !s.empty() && s[ 0 ].synced && s[ 0 ].decks[ 0 ].present && s[ 0 ].hasTrack[ 0 ] && !s[ 0 ].name.empty() )
			break;
		Sleep( 0.01 );
	}
	Sleep( 1.0 ); // a second of samples for the line (after a start or a jump it begins from one)
	start = Now();
	double jumpTime = jump ? start + seconds / 2 : -1;
	if( jump )
		traktor.jumpAt = jumpTime;
	while( Now() - start < seconds )
	{
		double now = Now();
		std::vector< Source > s = receiver.Sources( now );
		if( !s.empty() && s[ 0 ].decks[ 0 ].present )
		{
			m.found     = true;
			m.name      = s[ 0 ].name;
			m.synced    = s[ 0 ].synced;
			m.track     = s[ 0 ].hasTrack[ 0 ] && s[ 0 ].tracks[ 0 ].title == "Kerala";
			m.roundTrip = s[ 0 ].roundTrip;
			m.lost      = s[ 0 ].lost;
			double error = std::fabs( s[ 0 ].decks[ 0 ].position - traktor.Truth( now ) );
			bool nearJump = jump && now > jumpTime - 0.01 && now < jumpTime + 0.2;
			if( jump && m.followJump < 0 && now > jumpTime && s[ 0 ].decks[ 0 ].jumps > 0 && error < 0.05 )
				m.followJump = now - jumpTime;
			if( !nearJump )
			{
				sum += error;
				m.worst = std::max( m.worst, error );
				++count;
			}
		}
		Sleep( 1.0 / 60 );
	}
	m.mean = count ? sum / count : 1;
	return m;
}

static void TestOnThisComputer()
{
	std::string error;
	Sender sender;
	CHECK( sender.Start( "DJ-LAPTOP", error, 0 ) );
	// The receiver listens on a test port; the transmitter announces there too (its broadcasts go to the real one).
	Receiver receiver;
	CHECK( receiver.Start( "VJ-ARENA", error, 52190 ) );
	sender.AnnounceTo( { 0x7F000001, 52190 } );
	FakeTraktor traktor( sender );
	Measured m = Watch( receiver, traktor, 3.0, true );
	std::printf( "this computer: position error %.2f ms on average, %.2f ms at worst; jump followed in %.0f ms; round trip %.2f ms\n",
	             m.mean * 1000, m.worst * 1000, m.followJump * 1000, m.roundTrip * 1000 );
	CHECK( m.found && m.synced && m.track && m.name == "DJ-LAPTOP" );
	CHECK( m.mean < 0.005 );
	CHECK( m.worst < 0.020 );
	CHECK( m.followJump >= 0 && m.followJump < 0.1 );
	CHECK( m.roundTrip < 0.005 );
	// The transmitter knows who listens.
	std::vector< Sender::Subscriber > subs = sender.Subscribers();
	CHECK( subs.size() == 1 && subs[ 0 ].name == "VJ-ARENA" );
	// The transmitter goes away: the receiver says so at once.
	sender.Stop();
	Sleep( 0.1 );
	std::vector< Source > s = receiver.Sources( Now() );
	CHECK( s.size() == 1 && !s[ 0 ].online && !s[ 0 ].decks[ 0 ].present );
	receiver.Stop();
}

// Through a network where packets take 1..8 ms (Wi-Fi on a bad night) and 5% are lost; the receiver reaches the
// transmitter by address only, as when broadcasts do not get through.
static void TestBadNetwork()
{
	std::string error;
	Sender sender;
	CHECK( sender.Start( "DJ-LAPTOP", error, 0 ) );
	BadNetwork network( sender.Port(), 0.008, 0.05 );
	Receiver receiver;
	CHECK( receiver.Start( "VJ-ARENA", error, 52191 ) );
	receiver.SetAddresses( { "127.0.0.1:" + std::to_string( network.Port() ) } );
	FakeTraktor traktor( sender );
	Measured m = Watch( receiver, traktor, 4.0, true );
	std::printf( "bad network (1-8 ms, 5%% lost): position error %.2f ms on average, %.2f ms at worst; jump followed in %.0f ms; "
	             "round trip %.2f ms; %d packets lost\n",
	             m.mean * 1000, m.worst * 1000, m.followJump * 1000, m.roundTrip * 1000, m.lost );
	CHECK( m.found && m.synced && m.track );
	CHECK( m.mean < 0.006 );
	CHECK( m.worst < 0.030 );
	CHECK( m.followJump >= 0 && m.followJump < 0.15 );
	CHECK( m.lost > 0 );
	receiver.Stop();
	sender.Stop();
}

int main()
{
	TestPackets();
	TestTrackIds();
	TestClock();
	TestDeck();
	TestLeadingDeck();
	TestTraktorPosts();
	TestOnThisComputer();
	TestBadNetwork();
	if( failures )
		std::fprintf( stderr, "%d check(s) failed\n", failures );
	else
		std::printf( "vjlink_test: all passed\n" );
	return failures ? 1 : 0;
}
