#include "vjlink/TraktorBridge.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <json.hpp>
#include "vjlink/Net.h"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace vjlink
{
namespace
{
#ifdef _WIN32
using Native = SOCKET;
void CloseNative( Native s )
{
	closesocket( s );
}
#else
using Native = int;
void CloseNative( Native s )
{
	close( s );
}
#endif

double Number( const nlohmann::json& j, double fallback = 0 )
{
	if( j.is_number() )
	{
		double v = j.get< double >();
		return std::isfinite( v ) ? v : fallback;
	}
	if( j.is_boolean() )
		return j.get< bool >() ? 1 : 0;
	return fallback;
}

std::string Text( const nlohmann::json& j, const char* key )
{
	auto it = j.find( key );
	if( it == j.end() )
		return {};
	if( it->is_string() )
		return it->get< std::string >();
	if( it->is_number() )
		return it->dump();
	return {};
}

// The request's head and body, read with a deadline; false when it never came whole.
bool ReadRequest( Native s, std::string& head, std::string& body )
{
	std::string data;
	char buffer[ 4096 ];
	size_t headEnd = std::string::npos, length = 0;
	while( true )
	{
		if( headEnd != std::string::npos && data.size() >= headEnd + 4 + length )
			break;
		int n = (int)recv( s, buffer, (int)sizeof( buffer ), 0 );
		if( n <= 0 )
			return false;
		data.append( buffer, (size_t)n );
		if( data.size() > ( 1 << 20 ) )
			return false;
		if( headEnd == std::string::npos )
		{
			headEnd = data.find( "\r\n\r\n" );
			if( headEnd != std::string::npos )
			{
				std::string lower = data.substr( 0, headEnd );
				std::transform( lower.begin(), lower.end(), lower.begin(), []( unsigned char c ) { return (char)std::tolower( c ); } );
				size_t at = lower.find( "content-length:" );
				if( at != std::string::npos )
					length = (size_t)std::strtoul( lower.c_str() + at + 15, nullptr, 10 );
			}
		}
	}
	head = data.substr( 0, headEnd );
	body = data.substr( headEnd + 4, length );
	return true;
}
}

TraktorBridge::~TraktorBridge()
{
	Stop();
}

bool TraktorBridge::Start( DecksHandler decks, TrackHandler tracks, std::string& error, uint16_t port )
{
	Stop();
	onDecks = std::move( decks );
	onTrack = std::move( tracks );
	std::string unused;
	UdpSocket warm; // starts the network library on Windows
	warm.Open( 0, false, unused );
	warm.Close();
	Native s = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
#ifdef _WIN32
	if( s == INVALID_SOCKET )
#else
	if( s < 0 )
#endif
	{
		error = "cannot open a network socket";
		return false;
	}
	int yes = 1;
#ifdef _WIN32
	setsockopt( s, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&yes, sizeof( yes ) );
#else
	setsockopt( s, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof( yes ) );
#endif
	sockaddr_in a{};
	a.sin_family      = AF_INET;
	a.sin_addr.s_addr = htonl( 0x7F000001 ); // this computer only
	a.sin_port        = htons( port );
	if( bind( s, (sockaddr*)&a, sizeof( a ) ) != 0 || listen( s, 8 ) != 0 )
	{
		CloseNative( s );
		error = "port " + std::to_string( port ) + " is taken (is another transmitter or the old bridge running?)";
		return false;
	}
	listener = (intptr_t)s;
	running  = true;
	thread   = std::thread( [ this ] { Loop(); } );
	return true;
}

void TraktorBridge::Stop()
{
	if( !running )
		return;
	running = false;
	if( thread.joinable() )
		thread.join();
	CloseNative( (Native)listener );
	listener = -1;
}

double TraktorBridge::Silence() const
{
	return Now() - lastPost;
}

void TraktorBridge::Loop()
{
	while( running )
	{
		fd_set set;
		FD_ZERO( &set );
		FD_SET( (Native)listener, &set );
		timeval tv{ 0, 100000 };
		if( select( (int)listener + 1, &set, nullptr, nullptr, &tv ) <= 0 )
			continue;
		Native c = accept( (Native)listener, nullptr, nullptr );
#ifdef _WIN32
		if( c == INVALID_SOCKET )
			continue;
		DWORD timeout = 1000;
		setsockopt( c, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof( timeout ) );
#else
		if( c < 0 )
			continue;
		timeval timeout{ 1, 0 };
		setsockopt( c, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof( timeout ) );
#endif
		std::string head, body;
		bool whole = ReadRequest( c, head, body );
		double arrived = Now();
		if( whole && head.compare( 0, 4, "POST" ) == 0 )
		{
			// Answered first, so Traktor's script is free for the next post. "Connection: close": Traktor (Qt) must
			// not keep a connection that could hang if this program restarts during the night.
			const char* reply = "HTTP/1.1 204 No Content\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
			send( c, reply, (int)std::strlen( reply ), 0 );
			CloseNative( c );
			Parse( body, arrived );
			continue;
		}
		if( whole )
		{
			std::string text = "VJ LINK transmitter: " + std::to_string( posts.load() ) + " posts from Traktor, " +
			                   std::to_string( (int)std::lround( rate.load() ) ) + " a second.\n";
			std::string reply = "HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: " +
			                    std::to_string( text.size() ) + "\r\n\r\n" + text;
			send( c, reply.data(), (int)reply.size(), 0 );
		}
		CloseNative( c );
	}
}

bool TraktorBridge::Parse( const std::string& body, double arrived )
{
	nlohmann::json j = nlohmann::json::parse( body, nullptr, false );
	if( j.is_discarded() || !j.is_object() )
		return false;
	// Traktor may run the script twice: the session heard first is followed until it falls silent.
	std::string s = Text( j, "session" );
	if( s != session )
	{
		if( !session.empty() && arrived - sessionSeen < 2.0 )
			return true;
		session = s;
	}
	sessionSeen = arrived;
	lastPost    = arrived;
	++posts;
	++rateCount;
	if( arrived - rateStart >= 1.0 )
	{
		rate      = rateStart > 0 ? (float)( rateCount / ( arrived - rateStart ) ) : 0.0f;
		rateStart = arrived;
		rateCount = 0;
	}
	auto info = j.find( "info" );
	if( info != j.end() && info->is_array() && onTrack )
		for( int d = 0; d < kDecks && d < (int)info->size(); ++d )
		{
			const nlohmann::json& i = ( *info )[ (size_t)d ];
			if( !i.is_object() )
				continue;
			TrackInfo t;
			t.title  = Text( i, "title" );
			t.artist = Text( i, "artist" );
			t.key    = Text( i, "key" );
			t.file   = Text( i, "file" );
			t.entry  = (int64_t)Number( i.value( "entry", nlohmann::json() ) );
			t.length = Number( i.value( "length", nlohmann::json() ) );
			t.bpm    = (float)Number( i.value( "bpm", nlohmann::json() ) );
			t.stems  = Number( i.value( "stems", nlohmann::json() ) ) != 0;
			auto cues = i.find( "hotcues" );
			if( cues != i.end() && cues->is_array() )
				for( int h = 0; h < kHotcues && h < (int)cues->size(); ++h )
				{
					const nlohmann::json& c = ( *cues )[ (size_t)h ];
					if( !c.is_array() || c.size() < 3 )
						continue;
					Hotcue& hc = t.hotcues[ h ];
					hc.exists  = true;
					hc.type    = (int)Number( c[ 0 ], -1 );
					hc.start   = Number( c[ 1 ] );
					hc.length  = Number( c[ 2 ] );
					if( c.size() > 3 && c[ 3 ].is_string() )
						hc.name = c[ 3 ].get< std::string >();
				}
			onTrack( d, t );
		}
	auto fast = j.find( "fast" );
	if( fast == j.end() || !fast->is_array() )
		return true;
	DeckState decks[ kDecks ];
	for( int d = 0; d < kDecks && d < (int)fast->size(); ++d )
	{
		const nlohmann::json& f = ( *fast )[ (size_t)d ];
		if( !f.is_array() || f.size() < 16 )
			continue;
		DeckState& s = decks[ d ];
		s.loaded     = Number( f[ 0 ] ) != 0;
		s.playing    = Number( f[ 1 ] ) != 0;
		s.position   = Number( f[ 2 ] );
		s.rate       = (float)Number( f[ 3 ], 1 );
		s.beatPhase  = (float)Number( f[ 4 ] );
		s.beat       = Number( f[ 5 ] );
		s.bpm        = (float)Number( f[ 6 ] );
		s.inLoop     = Number( f[ 7 ] ) != 0;
		s.loopStart  = Number( f[ 8 ] );
		s.loopLength = Number( f[ 9 ] );
		for( int k = 0; k < kStems; ++k )
			s.stems[ k ] = (float)std::clamp( Number( f[ 10 + (size_t)k ] ), 0.0, 1.0 );
		s.level  = (float)std::clamp( Number( f[ 14 ] ), 0.0, 1.0 );
		s.volume = (float)std::clamp( Number( f[ 15 ] ), 0.0, 1.0 );
	}
	if( onDecks )
		onDecks( decks, arrived );
	return true;
}
}
