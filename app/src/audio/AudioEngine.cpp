#include "audio/AudioEngine.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <miniaudio.h>

namespace stltvj
{
namespace
{
constexpr double kAhead   = 0.4;  // seconds decoded in advance
constexpr double kSlack   = 0.12; // how far the sound may drift from the picture before it jumps to it
constexpr int kBlock      = 1024; // frames decoded at a time
}

// ---- voice

std::shared_ptr< AudioVoice > AudioVoice::Open( const std::string& path, int sampleRate )
{
	std::shared_ptr< AudioVoice > voice( new AudioVoice );
	if( sampleRate <= 0 || !voice->decoder.Open( path, sampleRate ) )
		return nullptr;
	voice->rate = sampleRate;
	voice->ring.resize( (size_t)( sampleRate * kAhead * 1.5 + kBlock ) * 2 );
	voice->worker = std::thread( [ v = voice.get() ] { v->Run(); } );
	return voice;
}

AudioVoice::~AudioVoice()
{
	{
		std::lock_guard< std::mutex > lock( mutex );
		quit = true;
	}
	wake.notify_all();
	if( worker.joinable() )
		worker.join();
}

double AudioVoice::PlayheadLocked() const
{
	return bufferStart + fraction / rate;
}

double AudioVoice::Playhead() const
{
	std::lock_guard< std::mutex > lock( mutex );
	return PlayheadLocked();
}

double AudioVoice::Buffered() const
{
	std::lock_guard< std::mutex > lock( mutex );
	return ( count - std::min( (double)count, fraction ) ) / rate;
}

void AudioVoice::Follow( double seconds, double newSpeed, float newGain )
{
	std::lock_guard< std::mutex > lock( mutex );
	gain  = newGain;
	speed = newSpeed;
	if( newSpeed <= 0.01 || newSpeed > 8 )
		return; // paused or backwards: silent, and the jump comes when it plays forwards again
	if( !started || std::fabs( PlayheadLocked() - seconds ) > kSlack )
	{
		started     = true;
		seekTo      = seconds;
		bufferStart = seconds;
		fraction    = 0;
		head = count = 0;
		generation++;
		wake.notify_one();
	}
}

void AudioVoice::Run()
{
	std::vector< float > block( kBlock * 2 );
	size_t capacity = ring.size() / 2;
	size_t wanted   = (size_t)( rate * kAhead );
	for( ;; )
	{
		double target = -1;
		int myGeneration;
		{
			std::unique_lock< std::mutex > lock( mutex );
			wake.wait( lock, [ & ] { return quit || seekTo >= 0 || ( started && count < wanted ); } );
			if( quit )
				return;
			target       = seekTo;
			seekTo       = -1;
			myGeneration = generation;
		}
		if( target >= 0 )
			decoder.Seek( target );
		int n = decoder.Read( block.data(), kBlock );
		std::unique_lock< std::mutex > lock( mutex );
		if( generation != myGeneration )
			continue; // jumped meanwhile: this block is from before
		if( n == 0 )
		{
			// The end of the sound: silence until the clip jumps back.
			wake.wait( lock, [ & ] { return quit || seekTo >= 0; } );
			continue;
		}
		size_t room = capacity - count;
		size_t take = std::min( (size_t)n, room );
		for( size_t i = 0; i < take; ++i )
		{
			size_t at          = ( head + count + i ) % capacity;
			ring[ at * 2 ]     = block[ i * 2 ];
			ring[ at * 2 + 1 ] = block[ i * 2 + 1 ];
		}
		count += take;
	}
}

void AudioVoice::Mix( float* out, int frames )
{
	// The other holders (the decoder's copy, the main loop's Follow) keep it for microseconds: waiting is better
	// than a gap in the sound.
	std::lock_guard< std::mutex > lock( mutex );
	float target = speed > 0.01 && speed <= 8 ? gain : 0.0f;
	if( target == 0 && lastGain == 0 )
		return;
	size_t capacity = ring.size() / 2;
	double step     = target > 0 ? speed : 1.0; // fading out at the old pace
	int i           = 0;
	for( ; i < frames; ++i )
	{
		// Linear interpolation between the two frames around the read position.
		size_t whole = (size_t)fraction;
		if( whole + 1 >= count )
			break; // not decoded yet
		double t   = fraction - whole;
		size_t a   = ( head + whole ) % capacity, b = ( head + whole + 1 ) % capacity;
		float g    = lastGain + ( target - lastGain ) * ( i + 1 ) / frames; // no clicks on changes
		out[ i * 2 ] += g * (float)( ring[ a * 2 ] * ( 1 - t ) + ring[ b * 2 ] * t );
		out[ i * 2 + 1 ] += g * (float)( ring[ a * 2 + 1 ] * ( 1 - t ) + ring[ b * 2 + 1 ] * t );
		fraction += step;
	}
	lastGain = i == frames ? target : lastGain;
	// Give back what was played.
	size_t used = std::min( (size_t)fraction, count );
	head        = ( head + used ) % capacity;
	count -= used;
	fraction -= used;
	bufferStart += used / (double)rate;
	if( count < (size_t)( rate * kAhead ) )
		wake.notify_one();
}

// ---- engine

struct AudioEngine::Device
{
	ma_context context;
	ma_device device;
	bool contextOpen = false;
	bool open        = false;
};

std::vector< std::string > AudioEngine::OutputDevices()
{
	std::vector< std::string > names;
	ma_context context;
	if( ma_context_init( nullptr, 0, nullptr, &context ) != MA_SUCCESS )
		return names;
	ma_device_info* playback = nullptr;
	ma_uint32 count          = 0;
	if( ma_context_get_devices( &context, &playback, &count, nullptr, nullptr ) == MA_SUCCESS )
		for( ma_uint32 i = 0; i < count; ++i )
			names.push_back( playback[ i ].name );
	ma_context_uninit( &context );
	return names;
}

AudioEngine::AudioEngine() = default;

AudioEngine::~AudioEngine()
{
	Stop();
}

bool AudioEngine::Start( const std::string& wanted, int bufferFrames )
{
	int keepRate = sampleRate;
	if( device )
	{
		// Started again: the old output closes; the voices come back with the next SetVoices.
		if( device->open )
			ma_device_uninit( &device->device );
		if( device->contextOpen )
			ma_context_uninit( &device->context );
		device.reset();
	}
	device                       = std::make_unique< Device >();
	ma_device_config config      = ma_device_config_init( ma_device_type_playback );
	config.playback.format       = ma_format_f32;
	config.playback.channels     = 2;
	config.sampleRate            = keepRate > 0 ? keepRate : 0; // the device's own, the first time
	config.periodSizeInFrames    = (ma_uint32)std::clamp( bufferFrames, 64, 4096 ); // 256: about 5 ms
	// A named output: found among the context's devices (the default when it is not there).
	if( ma_context_init( nullptr, 0, nullptr, &device->context ) == MA_SUCCESS )
	{
		device->contextOpen      = true;
		ma_device_info* playback = nullptr;
		ma_uint32 count          = 0;
		if( !wanted.empty() && ma_context_get_devices( &device->context, &playback, &count, nullptr, nullptr ) == MA_SUCCESS )
			for( ma_uint32 i = 0; i < count; ++i )
				if( wanted == playback[ i ].name )
					config.playback.pDeviceID = &playback[ i ].id;
	}
	config.dataCallback          = []( ma_device* d, void* out, const void*, ma_uint32 frames ) {
		AudioEngine* engine = (AudioEngine*)d->pUserData;
		std::memset( out, 0, (size_t)frames * 2 * sizeof( float ) );
		engine->Render( (float*)out, (int)frames );
	};
	config.pUserData = this;
	if( ma_device_init( device->contextOpen ? &device->context : nullptr, &config, &device->device ) != MA_SUCCESS )
	{
		if( device->contextOpen )
			ma_context_uninit( &device->context );
		device.reset();
		status = "no audio output available";
		return false;
	}
	device->open = true;
	sampleRate   = (int)device->device.sampleRate;
	if( ma_device_start( &device->device ) != MA_SUCCESS )
	{
		Stop();
		status = "the audio output would not start";
		return false;
	}
	deviceName = device->device.playback.name;
	status     = deviceName + ", " + std::to_string( sampleRate ) + " Hz, buffer " + std::to_string( (int)config.periodSizeInFrames );
	return true;
}

void AudioEngine::StartHeadless( int rate )
{
	sampleRate = rate;
	status     = "headless";
}

void AudioEngine::Stop()
{
	if( device && device->open )
		ma_device_uninit( &device->device );
	if( device && device->contextOpen )
		ma_context_uninit( &device->context );
	device.reset();
	std::lock_guard< std::mutex > lock( mutex );
	voices.clear();
	playing.clear();
	retired[ 0 ].clear();
	retired[ 1 ].clear();
}

void AudioEngine::SetVoices( std::vector< std::shared_ptr< AudioVoice > > list, float volume )
{
	std::vector< std::shared_ptr< AudioVoice > > freed;
	{
		std::lock_guard< std::mutex > lock( mutex );
		freed        = std::move( retired[ 1 ] );
		retired[ 1 ] = std::move( retired[ 0 ] );
		retired[ 0 ] = std::move( voices );
		voices       = std::move( list );
		master       = std::clamp( volume, 0.0f, 1.0f );
	}
	// `freed` goes here, on the main thread, after the audio thread has moved on to newer lists.
}

void AudioEngine::Render( float* out, int frames )
{
	float volume = lastMaster;
	{
		std::unique_lock< std::mutex > lock( mutex, std::try_to_lock );
		if( lock.owns_lock() )
		{
			playing = voices; // the voices this thread plays until the next update
			volume  = master;
		}
	}
	for( const std::shared_ptr< AudioVoice >& voice : playing )
		voice->Mix( out, frames );
	for( int i = 0; i < frames * 2; ++i )
	{
		float g     = lastMaster + ( volume - lastMaster ) * ( i / 2 + 1 ) / frames;
		out[ i ]    = std::clamp( out[ i ] * g, -1.0f, 1.0f );
	}
	lastMaster = volume;
}
}
