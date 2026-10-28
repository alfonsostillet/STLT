// Tests for clip sound: decoding to stereo, seeking to the sample, and voices following a clip in the mix.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>
#include "audio/AudioEngine.h"
#include "media/AudioDecoder.h"
#include "media/FrameSource.h"

using namespace stltvj;

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

static std::string Fixture( const char* name )
{
	return std::string( FIXTURES_DIR ) + "/" + name;
}

// Rising zero crossings of the left channel: a 440 Hz tone gives 44 per tenth of a second.
static int Crossings( const std::vector< float >& stereo, size_t frames )
{
	int n = 0;
	for( size_t i = 1; i < frames; ++i )
		if( stereo[ ( i - 1 ) * 2 ] < 0 && stereo[ i * 2 ] >= 0 )
			n++;
	return n;
}

static float Peak( const std::vector< float >& stereo, size_t frames )
{
	float peak = 0;
	for( size_t i = 0; i < frames * 2; ++i )
		peak = std::max( peak, std::fabs( stereo[ i ] ) );
	return peak;
}

static void TestDecoder()
{
	AudioDecoder none;
	CHECK( !none.Open( Fixture( "ramp_h264.mp4" ), 48000 ) ); // no sound in it

	// tone_aac.mp4: 2 s of a 440 Hz sine in mono AAC at 44.1 kHz, with a small picture.
	AudioDecoder tone;
	CHECK( tone.Open( Fixture( "tone_aac.mp4" ), 48000 ) );
	CHECK( std::fabs( tone.Duration() - 2 ) < 0.05 && tone.SampleRate() == 48000 );
	std::vector< float > buffer( 48000 * 2 );
	CHECK( tone.Read( buffer.data(), 4800 ) == 4800 );
	int c = Crossings( buffer, 4800 );
	CHECK( c >= 42 && c <= 46 );
	float peak = Peak( buffer, 4800 );
	CHECK( peak > 0.05f && peak < 0.3f );
	CHECK( std::fabs( buffer[ 1000 ] - buffer[ 1001 ] ) < 1e-6f ); // mono on both sides
	CHECK( std::fabs( tone.Position() - 0.1 ) < 0.001 ); // the file's first sample comes a fraction of a millisecond in

	// Seeking lands on the sample asked for, and the end comes after the remaining half second.
	tone.Seek( 1.5 );
	CHECK( std::fabs( tone.Position() - 1.5 ) < 1e-6 );
	int got = tone.Read( buffer.data(), 48000 );
	CHECK( got > 48000 * 0.45 && got < 48000 * 0.56 );
	tone.Seek( 0.25 );
	CHECK( tone.Read( buffer.data(), 4800 ) == 4800 );
	c = Crossings( buffer, 4800 );
	CHECK( c >= 42 && c <= 46 );
}

static void Render( AudioEngine& engine, std::vector< float >& out, int frames )
{
	std::fill( out.begin(), out.begin() + frames * 2, 0.0f );
	engine.Render( out.data(), frames );
}

// The voice decodes ahead on its own thread: wait until a quarter of a second is ready (blocks come every few
// milliseconds in real use; here they come back to back).
static void Settle( AudioVoice& voice )
{
	for( int i = 0; i < 500 && voice.Buffered() < 0.25; ++i )
		std::this_thread::sleep_for( std::chrono::milliseconds( 10 ) );
}

static void TestVoices()
{
	CHECK( AudioVoice::Open( Fixture( "ramp_h264.mp4" ), 48000 ) == nullptr );
	AudioEngine engine;
	engine.StartHeadless( 48000 );
	std::shared_ptr< AudioVoice > voice = AudioVoice::Open( Fixture( "tone_aac.mp4" ), 48000 );
	CHECK( voice != nullptr );
	if( !voice )
		return;
	std::vector< float > out( 48000 * 2 );

	// Playing from 0.5 s: the sound plays and the playhead moves with it.
	voice->Follow( 0.5, 1.0, 1.0f );
	engine.SetVoices( { voice }, 1.0f );
	Settle( *voice );
	Render( engine, out, 4800 );
	int c = Crossings( out, 4800 );
	CHECK( c >= 41 && c <= 46 );
	CHECK( Peak( out, 4800 ) > 0.05f );
	CHECK( std::fabs( voice->Playhead() - 0.6 ) < 0.005 );

	// Twice as fast: an octave up, twice the ground covered.
	voice->Follow( voice->Playhead(), 2.0, 1.0f );
	Render( engine, out, 4800 );
	c = Crossings( out, 4800 );
	CHECK( c >= 84 && c <= 92 );
	CHECK( std::fabs( voice->Playhead() - 0.8 ) < 0.01 );

	// Half the volume, then the master at a quarter.
	voice->Follow( voice->Playhead(), 1.0, 0.5f );
	Settle( *voice );
	Render( engine, out, 4800 ); // the change fades in over this block
	Render( engine, out, 4800 );
	float half = Peak( out, 4800 );
	engine.SetVoices( { voice }, 0.25f );
	voice->Follow( voice->Playhead(), 1.0, 1.0f );
	Settle( *voice );
	Render( engine, out, 4800 );
	Render( engine, out, 4800 );
	CHECK( std::fabs( Peak( out, 4800 ) - half / 2 ) < 0.01f );
	engine.SetVoices( { voice }, 1.0f );

	// Backwards (or paused): silent after a short fade, and the playhead stays.
	double before = voice->Playhead();
	voice->Follow( before, -1.0, 1.0f );
	for( int i = 0; i < 4; ++i )
		Render( engine, out, 1024 ); // the fade-out, then silence
	Render( engine, out, 4800 );
	CHECK( Peak( out, 4800 ) == 0.0f );

	// A jump (a loop, a cue point): the sound goes there.
	voice->Follow( 1.7, 1.0, 1.0f );
	CHECK( std::fabs( voice->Playhead() - 1.7 ) < 1e-6 );
	Settle( *voice );
	Render( engine, out, 4800 );
	CHECK( Peak( out, 4800 ) > 0.05f );
	CHECK( std::fabs( voice->Playhead() - 1.8 ) < 0.005 );

	// Small drift is left alone; out of the list, the voice is not heard.
	voice->Follow( 1.85, 1.0, 1.0f );
	CHECK( std::fabs( voice->Playhead() - 1.8 ) < 0.005 );
	engine.SetVoices( {}, 1.0f );
	Render( engine, out, 4800 );
	CHECK( Peak( out, 4800 ) == 0.0f );
}

int main()
{
	if( !HasFfmpeg() )
	{
		std::printf( "built without FFmpeg: no sound to test\n" );
		return 0;
	}
	TestDecoder();
	TestVoices();
	if( failures )
	{
		std::fprintf( stderr, "%d check(s) failed\n", failures );
		return 1;
	}
	std::printf( "audio tests passed\n" );
	return 0;
}
