#include "control/TimecodeInput.h"
#include <algorithm>
#include <chrono>
#include <miniaudio.h>
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
constexpr int kArtNetPort = 6454;

double SteadyNow()
{
	return std::chrono::duration< double >( std::chrono::steady_clock::now().time_since_epoch() ).count();
}

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

struct TimecodeInput::Audio
{
	ma_context context;
	ma_device device;
	bool contextOpen = false;
	bool open        = false;
};

const char* TimecodeInput::SourceName( Source source )
{
	static const char* names[] = { "Off", "LTC (audio)", "MTC (MIDI)", "Art-Net" };
	return names[ std::clamp( (int)source, 0, (int)Source::Count - 1 ) ];
}

TimecodeInput::TimecodeInput() = default;

TimecodeInput::~TimecodeInput()
{
	Close();
}

void TimecodeInput::Close()
{
	if( audio )
	{
		if( audio->open )
			ma_device_uninit( &audio->device );
		if( audio->contextOpen )
			ma_context_uninit( &audio->context );
		audio.reset();
	}
	audioName.clear();
	if( artThread.joinable() )
	{
		artRunning = false;
		artThread.join();
	}
	if( artSocket != -1 )
		CloseSocket( (Socket)artSocket );
	artSocket = -1;
	error.clear();
	receiver.Reset();
}

void TimecodeInput::Configure( Source wanted, const std::string& device, int channel, const std::string& midiInput )
{
	{
		std::lock_guard< std::mutex > lock( mtcMutex );
		mtcInput = midiInput;
	}
	if( wanted == source && device == ltcDevice && channel == ltcChannel && error.empty() )
		return;
	Close();
	source     = wanted;
	ltcDevice  = device;
	ltcChannel = channel;

	if( source == Source::Ltc )
	{
		audio                   = std::make_unique< Audio >();
		ma_device_config config = ma_device_config_init( ma_device_type_capture );
		config.capture.format   = ma_format_f32;
		config.capture.channels = 0; // the device's own: LTC is on one of them
		if( ma_context_init( nullptr, 0, nullptr, &audio->context ) == MA_SUCCESS )
		{
			audio->contextOpen      = true;
			ma_device_info* capture = nullptr;
			ma_uint32 count         = 0;
			if( !device.empty() && ma_context_get_devices( &audio->context, nullptr, nullptr, &capture, &count ) == MA_SUCCESS )
				for( ma_uint32 i = 0; i < count; ++i )
					if( device == capture[ i ].name )
						config.capture.pDeviceID = &capture[ i ].id;
		}
		config.dataCallback = []( ma_device* d, void*, const void* in, ma_uint32 frames ) {
			TimecodeInput* self = (TimecodeInput*)d->pUserData;
			if( in )
				self->ltc.Feed( (const float*)in + self->channelUsed, frames, (int)d->sampleRate, SteadyNow(), self->channels );
		};
		config.pUserData = this;
		if( ma_device_init( audio->contextOpen ? &audio->context : nullptr, &config, &audio->device ) != MA_SUCCESS )
		{
			error = "the audio input would not open";
			return;
		}
		audio->open = true;
		channels    = std::max( 1, (int)audio->device.capture.channels );
		channelUsed = std::clamp( channel, 0, channels - 1 );
		if( ma_device_start( &audio->device ) != MA_SUCCESS )
		{
			error = "the audio input would not start";
			return;
		}
		audioName = audio->device.capture.name;
	}
	else if( source == Source::ArtNet )
	{
		if( !SocketsReady() )
		{
			error = "network not available";
			return;
		}
		Socket s = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
		if( s == (Socket)-1 )
		{
			error = "cannot open a network socket";
			return;
		}
		// Lighting software on this computer may listen on Art-Net's port too: share it.
		int yes = 1;
		setsockopt( s, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof( yes ) );
		setsockopt( s, SOL_SOCKET, SO_BROADCAST, (const char*)&yes, sizeof( yes ) );
		sockaddr_in address{};
		address.sin_family      = AF_INET;
		address.sin_addr.s_addr = htonl( INADDR_ANY );
		address.sin_port        = htons( (unsigned short)kArtNetPort );
		if( bind( s, (sockaddr*)&address, sizeof( address ) ) != 0 )
		{
			CloseSocket( s );
			error = "port 6454 is taken by another program";
			return;
		}
#ifdef _WIN32
		DWORD timeout = 200;
		setsockopt( s, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof( timeout ) );
#else
		timeval timeout{ 0, 200000 };
		setsockopt( s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof( timeout ) );
#endif
		artSocket  = (intptr_t)s;
		artRunning = true;
		artThread  = std::thread( [ this, s ] {
            uint8_t buffer[ 1024 ];
            while( artRunning )
            {
                int n = (int)recv( s, (char*)buffer, (int)sizeof( buffer ), 0 );
                Timecode tc;
                if( n > 0 && ParseArtTimecode( buffer, (size_t)n, tc ) )
                    receiver.Receive( tc, SteadyNow() );
            }
        } );
	}
}

void TimecodeInput::OnMidi( const std::string& device, const unsigned char* bytes, size_t size, double time )
{
	if( source != Source::Mtc || !size || ( bytes[ 0 ] != 0xF1 && bytes[ 0 ] != 0xF0 ) )
		return;
	std::lock_guard< std::mutex > lock( mtcMutex );
	if( mtcInput != "*" && mtcInput != device )
		return;
	mtc.Message( bytes, size, time );
}

std::string TimecodeInput::Status() const
{
	if( source == Source::Off )
		return "";
	if( !error.empty() )
		return error;
	TimecodeReceiver::State state = receiver.At( SteadyNow(), 0 );
	Source current    = source;
	std::string where = current == Source::Ltc ? audioName + ", channel " + std::to_string( channelUsed + 1 ) + " of " + std::to_string( channels )
	                    : current == Source::Mtc ? ( mtcInput == "*" ? std::string( "any MIDI input" ) : mtcInput )
	                                            : std::string( "UDP port 6454" );
	if( !state.present )
		return "Waiting for timecode (" + where + ")";
	return state.last.Text() + "  " + TimecodeRateName( state.last.rate ) + ( state.running ? "" : ", stopped" ) + "  (" + where + ")";
}
}
