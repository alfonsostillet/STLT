#include "control/RemoteInputs.h"
#include <RtMidi.h>
#include <algorithm>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

namespace stltvj
{
namespace
{
#ifdef _WIN32
using Socket = SOCKET;
void CloseSocket( Socket s )
{
	closesocket( s );
}
bool SocketsReady()
{
	static bool ready = [] {
		WSADATA data;
		return WSAStartup( MAKEWORD( 2, 2 ), &data ) == 0;
	}();
	return ready;
}
#else
using Socket = int;
void CloseSocket( Socket s )
{
	close( s );
}
bool SocketsReady()
{
	return true;
}
#endif
}

RemoteInputs::RemoteInputs() = default;

RemoteInputs::MidiPort::~MidiPort() = default;

RemoteInputs::~RemoteInputs()
{
	StopOsc();
	midiPorts.clear();
}

void RemoteInputs::Push( Event event )
{
	std::lock_guard< std::mutex > lock( mutex );
	if( queue.size() < 4096 ) // nobody polling: do not grow without end
		queue.push_back( std::move( event ) );
}

std::vector< RemoteInputs::Event > RemoteInputs::Poll()
{
	std::lock_guard< std::mutex > lock( mutex );
	std::vector< Event > events;
	events.swap( queue );
	return events;
}

// ---- OSC

void RemoteInputs::StartOsc( int port )
{
	StopOsc();
	oscPort  = port;
	oscError.clear();
	if( !SocketsReady() )
	{
		oscError = "network not available";
		return;
	}
	Socket s = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	if( s == (Socket)-1 )
	{
		oscError = "cannot open a network socket";
		return;
	}
	sockaddr_in address{};
	address.sin_family      = AF_INET;
	address.sin_addr.s_addr = htonl( INADDR_ANY );
	address.sin_port        = htons( (unsigned short)port );
	if( bind( s, (sockaddr*)&address, sizeof( address ) ) != 0 )
	{
		CloseSocket( s );
		oscError = "port " + std::to_string( port ) + " is taken by another program";
		return;
	}
	// Wake up a few times a second to notice a stop.
#ifdef _WIN32
	DWORD timeout = 200;
	setsockopt( s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof( timeout ) );
#else
	timeval timeout{ 0, 200000 };
	setsockopt( s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof( timeout ) );
#endif
	oscSocket  = (intptr_t)s;
	oscRunning = true;
	oscThread  = std::thread( [ this, s ] {
        std::vector< uint8_t > buffer( 65536 );
        while( oscRunning )
        {
            int n = (int)recv( s, (char*)buffer.data(), (int)buffer.size(), 0 );
            if( n <= 0 )
                continue;
            std::vector< OscMessage > messages;
            if( !ParseOsc( buffer.data(), (size_t)n, messages ) )
                continue;
            for( OscMessage& m : messages )
            {
                Event e;
                e.address = std::move( m.address );
                e.values  = std::move( m.values );
                Push( std::move( e ) );
            }
        }
    } );
}

void RemoteInputs::StopOsc()
{
	if( oscThread.joinable() )
	{
		oscRunning = false;
		oscThread.join();
	}
	if( oscSocket != -1 )
		CloseSocket( (Socket)oscSocket );
	oscSocket = -1;
	oscPort   = 0;
}

std::string RemoteInputs::OscStatus() const
{
	if( !oscError.empty() )
		return oscError;
	return oscPort ? "listening on UDP port " + std::to_string( oscPort ) : "off";
}

// ---- MIDI

std::string MidiDeviceName( const std::string& name, bool numbered )
{
	if( !numbered )
		return name;
	size_t space = name.find_last_of( ' ' );
	if( space == std::string::npos || space == 0 || space + 1 == name.size() )
		return name;
	for( size_t i = space + 1; i < name.size(); ++i )
		if( name[ i ] < '0' || name[ i ] > '9' )
			return name;
	return name.substr( 0, space );
}

void RemoteInputs::OnMidi( double, std::vector< unsigned char >* message, void* user )
{
	MidiPort* port = (MidiPort*)user;
	Event e;
	e.midi   = true;
	e.device = port->name;
	if( message && ReadMidi( *message, e.key, e.value ) )
		port->owner->Push( std::move( e ) );
}

void RemoteInputs::Configure( const ControlSettings& settings, bool retry )
{
	int wantPort = settings.oscEnabled ? settings.oscPort : 0;
	if( wantPort != oscPort || ( wantPort && !oscError.empty() && !oscRunning ) )
	{
		if( wantPort )
			StartOsc( wantPort );
		else
		{
			StopOsc();
			oscError.clear();
		}
	}

	// MIDI: open the inputs that are on, close the rest.
	if( midiUnavailable && !retry )
		return;
	// Each input by its name (MidiDeviceName), with the port numbers it shows up at: Windows can list one device
	// more than once, and only one of them opens.
	std::vector< std::string > names;
	std::map< std::string, std::vector< unsigned int > > portsOf;
	try
	{
		RtMidiIn probe;
		unsigned int count = probe.getPortCount();
		for( unsigned int i = 0; i < count; ++i )
		{
			std::string name = MidiDeviceName( probe.getPortName( i ) );
			if( !portsOf.count( name ) )
				names.push_back( name );
			portsOf[ name ].push_back( i );
		}
		midiError.clear();
		midiUnavailable = false;
	}
	catch( const RtMidiError& error )
	{
		midiError       = "MIDI not available: " + error.getMessage();
		midiUnavailable = true;
	}
	devices    = names;
	auto isOff = [ & ]( const std::string& name ) {
		return std::find( settings.midiInputsOff.begin(), settings.midiInputsOff.end(), name ) != settings.midiInputsOff.end();
	};
	for( auto it = midiPorts.begin(); it != midiPorts.end(); )
	{
		bool off = !settings.midiEnabled || !portsOf.count( it->first ) || isOff( it->first );
		it       = off ? midiPorts.erase( it ) : std::next( it );
	}
	if( !settings.midiEnabled )
		return;
	for( const std::string& name : names )
	{
		if( midiPorts.count( name ) || isOff( name ) )
			continue;
		std::string failure;
		for( unsigned int index : portsOf[ name ] )
		{
			try
			{
				auto port   = std::make_unique< MidiPort >();
				port->owner = this;
				port->name  = name;
				port->in    = std::make_unique< RtMidiIn >();
				port->in->openPort( index, "STLT-VJ" );
				port->in->ignoreTypes( true, true, true ); // no system exclusive, clock or active sensing
				port->in->setCallback( &RemoteInputs::OnMidi, port.get() );
				midiPorts[ name ] = std::move( port );
				failure.clear();
				break;
			}
			catch( const RtMidiError& error )
			{
				failure = error.getMessage(); // perhaps another listing of the same device: try the next
			}
		}
		if( !failure.empty() )
			midiError = name + ": " + failure;
	}
}
}
