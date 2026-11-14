// Tests for clip sound: decoding to stereo, seeking to the sample, and voices following a clip in the mix.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>
#include "audio/AudioEngine.h"
#include "audio/AudioInput.h"
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

// Where the test is, printed before each step and flushed at once: a crash (no output of its own) is then
// located by the last line before it.
static void Step( const char* what )
{
	std::printf( "-- %s\n", what );
	std::fflush( stdout );
}

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
	Step( "decoder: a file without sound" );
	AudioDecoder none;
	CHECK( !none.Open( Fixture( "ramp_h264.mp4" ), 48000 ) ); // no sound in it

	// tone_aac.mp4: 2 s of a 440 Hz sine in mono AAC at 44.1 kHz, with a small picture.
	Step( "decoder: open tone_aac.mp4" );
	AudioDecoder tone;
	CHECK( tone.Open( Fixture( "tone_aac.mp4" ), 48000 ) );
	CHECK( std::fabs( tone.Duration() - 2 ) < 0.05 && tone.SampleRate() == 48000 );
	std::vector< float > buffer( 48000 * 2 );
	Step( "decoder: read the first tenth of a second" );
	CHECK( tone.Read( buffer.data(), 4800 ) == 4800 );
	int c = Crossings( buffer, 4800 );
	CHECK( c >= 42 && c <= 46 );
	float peak = Peak( buffer, 4800 );
	CHECK( peak > 0.05f && peak < 0.3f );
	CHECK( std::fabs( buffer[ 1000 ] - buffer[ 1001 ] ) < 1e-6f ); // mono on both sides
	CHECK( std::fabs( tone.Position() - 0.1 ) < 0.001 ); // the file's first sample comes a fraction of a millisecond in

	// Seeking lands on the sample asked for, and the end comes after the remaining half second.
	Step( "decoder: seek to 1.5 s and read to the end" );
	tone.Seek( 1.5 );
	CHECK( std::fabs( tone.Position() - 1.5 ) < 1e-6 );
	int got = tone.Read( buffer.data(), 48000 );
	CHECK( got > 48000 * 0.45 && got < 48000 * 0.56 );
	Step( "decoder: seek back to 0.25 s" );
	tone.Seek( 0.25 );
	CHECK( tone.Read( buffer.data(), 4800 ) == 4800 );
	c = Crossings( buffer, 4800 );
	CHECK( c >= 42 && c <= 46 );
	Step( "decoder: close" );
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
	Step( "voices: a file without sound" );
	CHECK( AudioVoice::Open( Fixture( "ramp_h264.mp4" ), 48000 ) == nullptr );
	Step( "voices: headless engine, open tone_aac.mp4 (starts the decoding thread)" );
	AudioEngine engine;
	engine.StartHeadless( 48000 );
	std::shared_ptr< AudioVoice > voice = AudioVoice::Open( Fixture( "tone_aac.mp4" ), 48000 );
	CHECK( voice != nullptr );
	if( !voice )
		return;
	std::vector< float > out( 48000 * 2 );

	Step( "voices: play from 0.5 s" );
	// Playing from 0.5 s: the sound plays and the playhead moves with it.
	voice->Follow( 0.5, 1.0, 1.0f );
	engine.SetVoices( { voice }, 1.0f );
	Settle( *voice );
	Render( engine, out, 4800 );
	int c = Crossings( out, 4800 );
	CHECK( c >= 41 && c <= 46 );
	CHECK( Peak( out, 4800 ) > 0.05f );
	CHECK( std::fabs( voice->Playhead() - 0.6 ) < 0.005 );

	Step( "voices: twice as fast" );
	// Twice as fast: an octave up, twice the ground covered.
	voice->Follow( voice->Playhead(), 2.0, 1.0f );
	Render( engine, out, 4800 );
	c = Crossings( out, 4800 );
	CHECK( c >= 84 && c <= 92 );
	CHECK( std::fabs( voice->Playhead() - 0.8 ) < 0.01 );

	Step( "voices: half the volume, then the master at a quarter" );
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

	Step( "voices: backwards" );
	// Backwards (or paused): silent after a short fade, and the playhead stays.
	double before = voice->Playhead();
	voice->Follow( before, -1.0, 1.0f );
	for( int i = 0; i < 4; ++i )
		Render( engine, out, 1024 ); // the fade-out, then silence
	Render( engine, out, 4800 );
	CHECK( Peak( out, 4800 ) == 0.0f );

	Step( "voices: a jump to 1.7 s" );
	// A jump (a loop, a cue point): the sound goes there.
	voice->Follow( 1.7, 1.0, 1.0f );
	CHECK( std::fabs( voice->Playhead() - 1.7 ) < 1e-6 );
	Settle( *voice );
	Render( engine, out, 4800 );
	CHECK( Peak( out, 4800 ) > 0.05f );
	CHECK( std::fabs( voice->Playhead() - 1.8 ) < 0.005 );

	Step( "voices: small drift, then out of the list" );
	// Small drift is left alone; out of the list, the voice is not heard.
	voice->Follow( 1.85, 1.0, 1.0f );
	CHECK( std::fabs( voice->Playhead() - 1.8 ) < 0.005 );
	engine.SetVoices( {}, 1.0f );
	Render( engine, out, 4800 );
	CHECK( Peak( out, 4800 ) == 0.0f );
	Step( "voices: release the voice and the engine (joins the decoding thread)" );
}

// The sound coming in, measured for parameters to follow: a tone lands in its band and the meters fall back.
static void TestInputBands()
{
	auto tone = []( float hz, float amplitude ) {
		std::vector< float > samples( 4096 );
		for( size_t i = 0; i < samples.size(); ++i )
			samples[ i ] = amplitude * (float)std::sin( 6.283185307 * hz * (double)i / 48000 );
		return samples;
	};
	Step( "input: bands of a tone fed by hand" );
	AudioInput input; // no device: the sound is handed in
	CHECK( !input.Running() );
	std::vector< float > bass = tone( 60, 0.5f );
	input.Feed( bass.data(), (int)bass.size(), 48000 );
	input.Analyse( 0 );
	// Half of full scale is -6 dB: 0.9 on the meter's -60..0 dB.
	CHECK( std::fabs( input.Value( AudioInput::Bass ) - 0.9f ) < 0.03f );
	CHECK( std::fabs( input.Value( AudioInput::Level ) - 0.9f ) < 0.03f );
	CHECK( input.Value( AudioInput::Mid ) < 0.2f );
	CHECK( input.Value( AudioInput::Treble ) < 0.05f );

	std::vector< float > high = tone( 5000, 1 );
	input.Feed( high.data(), (int)high.size(), 48000 );
	input.Analyse( 0.1 );
	CHECK( input.Value( AudioInput::Treble ) > 0.97f );
	CHECK( std::fabs( input.Value( AudioInput::Bass ) - 0.6f ) < 0.03f ); // falling: 0.3 in a tenth of a second
	input.Analyse( 1 );
	CHECK( input.Value( AudioInput::Bass ) < 0.05f );

	Step( "input: silence, then gain" );
	std::vector< float > silence( 4096, 0.0f );
	input.Feed( silence.data(), (int)silence.size(), 48000 );
	input.Analyse( 1 );
	for( int b = 0; b < AudioInput::BandCount; ++b )
		CHECK( input.Value( (AudioInput::Band)b ) == 0 );
	// Gain: -12 dB of signal raised by 12 dB reads as full scale.
	input.gain               = 3.98f;
	std::vector< float > low = tone( 1000, 0.25f );
	input.Feed( low.data(), (int)low.size(), 48000 );
	input.Analyse( 0 );
	CHECK( input.Value( AudioInput::Mid ) > 0.97f );
}

int main()
{
	Step( "TestInputBands" );
	TestInputBands();
	if( !HasFfmpeg() )
	{
		std::printf( "built without FFmpeg: no clip sound to test\n" );
		return failures ? 1 : 0;
	}
	Step( "TestDecoder" );
	TestDecoder();
	Step( "TestVoices" );
	TestVoices();
	if( failures )
	{
		std::fprintf( stderr, "%d check(s) failed\n", failures );
		return 1;
	}
	std::printf( "audio tests passed\n" );
	return 0;
}
