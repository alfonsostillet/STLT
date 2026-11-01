#include "audio/AudioInput.h"
#include <algorithm>
#include <cmath>
#include <complex>
#include <miniaudio.h>

namespace stltvj
{
namespace
{
// In-place radix-2 FFT of a power-of-two length.
void Fft( std::vector< std::complex< float > >& a )
{
	size_t n = a.size();
	for( size_t i = 1, j = 0; i < n; ++i )
	{
		size_t bit = n >> 1;
		for( ; j & bit; bit >>= 1 )
			j ^= bit;
		j ^= bit;
		if( i < j )
			std::swap( a[ i ], a[ j ] );
	}
	for( size_t len = 2; len <= n; len <<= 1 )
	{
		float angle = -6.2831853f / (float)len;
		std::complex< float > step( std::cos( angle ), std::sin( angle ) );
		for( size_t i = 0; i < n; i += len )
		{
			std::complex< float > w( 1, 0 );
			for( size_t k = 0; k < len / 2; ++k )
			{
				std::complex< float > u = a[ i + k ], v = a[ i + k + len / 2 ] * w;
				a[ i + k ]               = u + v;
				a[ i + k + len / 2 ]     = u - v;
				w *= step;
			}
		}
	}
}

// An amplitude (1: full scale) as 0..1 over -60..0 dB.
float Meter( float amplitude )
{
	if( amplitude <= 1e-6f )
		return 0;
	return std::clamp( ( 20.0f * std::log10( amplitude ) + 60.0f ) / 60.0f, 0.0f, 1.0f );
}
}

struct AudioInput::Device
{
	ma_context context;
	ma_device device;
	bool contextOpen = false;
	bool open        = false;
};

const char* AudioInput::BandName( Band band )
{
	static const char* names[] = { "Level", "Bass", "Mid", "Treble" };
	return names[ std::clamp( (int)band, 0, (int)BandCount - 1 ) ];
}

AudioInput::AudioInput() = default;

AudioInput::~AudioInput()
{
	Stop();
}

std::vector< std::string > AudioInput::Devices()
{
	std::vector< std::string > names;
	ma_context context;
	if( ma_context_init( nullptr, 0, nullptr, &context ) != MA_SUCCESS )
		return names;
	ma_device_info* capture = nullptr;
	ma_uint32 count         = 0;
	if( ma_context_get_devices( &context, nullptr, nullptr, &capture, &count ) == MA_SUCCESS )
		for( ma_uint32 i = 0; i < count; ++i )
			names.push_back( capture[ i ].name );
	ma_context_uninit( &context );
	return names;
}

bool AudioInput::Start( const std::string& wanted )
{
	Stop();
	device                  = std::make_unique< Device >();
	ma_device_config config = ma_device_config_init( ma_device_type_capture );
	config.capture.format   = ma_format_f32;
	config.capture.channels = 1; // mixed down by miniaudio
	if( ma_context_init( nullptr, 0, nullptr, &device->context ) == MA_SUCCESS )
	{
		device->contextOpen     = true;
		ma_device_info* capture = nullptr;
		ma_uint32 count         = 0;
		if( !wanted.empty() && ma_context_get_devices( &device->context, nullptr, nullptr, &capture, &count ) == MA_SUCCESS )
			for( ma_uint32 i = 0; i < count; ++i )
				if( wanted == capture[ i ].name )
					config.capture.pDeviceID = &capture[ i ].id;
	}
	config.dataCallback = []( ma_device* d, void*, const void* in, ma_uint32 frames ) {
		AudioInput* input = (AudioInput*)d->pUserData;
		input->Feed( (const float*)in, (int)frames, (int)d->sampleRate );
	};
	config.pUserData = this;
	if( ma_device_init( device->contextOpen ? &device->context : nullptr, &config, &device->device ) != MA_SUCCESS )
	{
		Stop();
		status = "no audio input available";
		return false;
	}
	device->open = true;
	if( ma_device_start( &device->device ) != MA_SUCCESS )
	{
		Stop();
		status = "the audio input would not start";
		return false;
	}
	status = std::string( device->device.capture.name ) + ", " + std::to_string( device->device.sampleRate ) + " Hz";
	return true;
}

void AudioInput::Stop()
{
	if( !device )
		return;
	if( device->open )
		ma_device_uninit( &device->device );
	if( device->contextOpen )
		ma_context_uninit( &device->context );
	device.reset();
	std::fill( std::begin( values ), std::end( values ), 0.0f );
	status.clear();
}

bool AudioInput::Running() const
{
	return device && device->open;
}

void AudioInput::Feed( const float* samples, int frames, int rate )
{
	if( !samples )
		return;
	std::lock_guard< std::mutex > lock( mutex );
	sampleRate = rate > 0 ? rate : sampleRate;
	for( int i = 0; i < frames; ++i )
	{
		ring[ write ] = samples[ i ];
		write         = ( write + 1 ) % kWindow;
	}
}

void AudioInput::Analyse( double seconds )
{
	std::vector< std::complex< float > > spectrum( kWindow );
	int rate = 48000;
	{
		std::lock_guard< std::mutex > lock( mutex );
		rate = sampleRate;
		for( int i = 0; i < kWindow; ++i )
			spectrum[ i ] = ring[ ( write + i ) % kWindow ] * gain;
	}
	// The level: the window's RMS, as a sine's peak (x √2), so a full-scale sine reads 0 dB.
	double square = 0;
	for( const std::complex< float >& s : spectrum )
		square += (double)s.real() * s.real();
	float measured[ BandCount ];
	measured[ Level ] = Meter( (float)std::sqrt( 2.0 * square / kWindow ) );
	// The bands, through a Hann window: each band's amplitude from its energy (a full-scale sine in a band: 0 dB).
	for( int i = 0; i < kWindow; ++i )
		spectrum[ i ] *= 0.5f - 0.5f * std::cos( 6.2831853f * (float)i / ( kWindow - 1 ) );
	Fft( spectrum );
	const float edges[][ 2 ] = { { 20, 200 }, { 200, 2000 }, { 2000, 12000 } };
	for( int b = 0; b < 3; ++b )
	{
		int from = std::max( 1, (int)std::floor( edges[ b ][ 0 ] * kWindow / rate ) );
		int to   = std::min( kWindow / 2, (int)std::ceil( edges[ b ][ 1 ] * kWindow / rate ) );
		double energy = 0;
		for( int k = from; k < to; ++k )
			energy += std::norm( spectrum[ k ] );
		// Hann's coherent gain is 0.5: a sine of amplitude A peaks at A * N / 4 in its bin, and its energy spreads
		// over neighbouring bins by Hann's noise bandwidth, 1.5 bins.
		float amplitude    = (float)( std::sqrt( energy / 1.5 ) * 4.0 / kWindow );
		measured[ 1 + b ] = Meter( amplitude );
	}
	float fall = (float)std::max( 0.0, seconds ) * 3.0f; // all the way down in a third of a second
	for( int b = 0; b < BandCount; ++b )
		values[ b ] = std::max( measured[ b ], values[ b ] - fall );
}
}
