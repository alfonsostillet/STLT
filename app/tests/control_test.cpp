// Tests for remote control: the address space, OSC packets, MIDI messages and the saved settings.
#include <cmath>
#include <cstdio>
#include <cstring>
#include "control/Control.h"
#include "control/Feedback.h"
#include "control/RemoteInputs.h"
#include "control/TempoSync.h"
#include "control/Timecode.h"
#include <vector>
#include <chrono>
#include <random>
#include <thread>

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

using Kind = ControlTarget::Kind;

static void TestAddresses()
{
	ControlTarget t;
	CHECK( ParseControlAddress( "/composition/layers/2/clips/3/connect", t ) && t.kind == Kind::ClipConnect && t.layer == 1 && t.column == 2 && t.IsButton() );
	CHECK( ParseControlAddress( "/composition/columns/4/connect", t ) && t.kind == Kind::ColumnConnect && t.column == 3 );
	CHECK( ParseControlAddress( "/composition/layers/1/video/opacity", t ) && t.kind == Kind::LayerOpacity && t.layer == 0 );
	CHECK( ParseControlAddress( "/composition/layers/1/bypassed", t ) && t.kind == Kind::LayerBypass && t.IsSwitch() );
	CHECK( ParseControlAddress( "/composition/layers/3/clear", t ) && t.kind == Kind::LayerClear && t.layer == 2 );
	CHECK( ParseControlAddress( "/composition/master", t ) && t.kind == Kind::Master );
	CHECK( ParseControlAddress( "/composition/video/opacity", t ) && t.kind == Kind::Master );
	CHECK( ParseControlAddress( "/composition/tempocontroller/tempotap", t ) && t.kind == Kind::TempoTap );
	CHECK( ParseControlAddress( "/composition/tempocontroller/tempo", t ) && t.kind == Kind::Tempo );
	CHECK( ParseControlAddress( "/composition/audio/volume", t ) && t.kind == Kind::AudioVolume );
	CHECK( ParseControlAddress( "/composition/layers/2/audio/volume", t ) && t.kind == Kind::LayerVolume && t.layer == 1 );
	CHECK( ParseControlAddress( "/composition/layers/2/clips/4/audio/volume", t ) && t.kind == Kind::ClipVolume && t.column == 3 );
	// Effect parameters in all three scopes.
	CHECK( ParseControlAddress( "/composition/video/effects/2/params/amount", t ) && t.kind == Kind::EffectParam && t.layer == -1 && t.slot == 1 && t.param == "amount" );
	CHECK( ParseControlAddress( "/composition/layers/4/video/effects/1/params/hue_shift", t ) && t.kind == Kind::EffectParam && t.layer == 3 && t.column == -1 && t.slot == 0 );
	CHECK( ParseControlAddress( "/composition/layers/1/clips/5/video/effects/3/params/x", t ) && t.kind == Kind::EffectParam && t.layer == 0 && t.column == 4 && t.slot == 2 );
	CHECK( EffectParamAddress( "/composition/layers/2", 0, "Hue Shift" ) == "/composition/layers/2/video/effects/1/params/hue_shift" );
	// Not ours, or out of range.
	CHECK( !ParseControlAddress( "/composition/layers/0/clear", t ) );
	CHECK( !ParseControlAddress( "/composition/layers/x/clear", t ) );
	CHECK( !ParseControlAddress( "/deck/1/play", t ) );
	CHECK( !ParseControlAddress( "/composition/layers/1/clips/2", t ) );
	CHECK( !ParseControlAddress( "", t ) );
}

static void TestOsc()
{
	std::vector< uint8_t > packet = MakeOscMessage( "/composition/layers/1/video/opacity", { 0.25f } );
	CHECK( packet.size() % 4 == 0 );
	std::vector< OscMessage > messages;
	CHECK( ParseOsc( packet.data(), packet.size(), messages ) && messages.size() == 1 );
	CHECK( messages[ 0 ].address == "/composition/layers/1/video/opacity" && messages[ 0 ].values.size() == 1 && messages[ 0 ].values[ 0 ] == 0.25f );

	// An integer, a true and no-argument message, in a bundle.
	auto pad = []( std::vector< uint8_t >& v, const char* s ) {
		size_t n = std::strlen( s );
		v.insert( v.end(), s, s + n );
		do
			v.push_back( 0 );
		while( v.size() % 4 );
	};
	std::vector< uint8_t > a;
	pad( a, "/composition/columns/2/connect" );
	pad( a, ",iT" );
	a.insert( a.end(), { 0, 0, 0, 1 } );
	std::vector< uint8_t > b;
	pad( b, "/composition/tempocontroller/tempotap" );
	std::vector< uint8_t > bundle;
	pad( bundle, "#bundle" );
	bundle.insert( bundle.end(), 8, 0 ); // time tag
	for( auto* m : { &a, &b } )
	{
		uint32_t n = (uint32_t)m->size();
		bundle.insert( bundle.end(), { (uint8_t)( n >> 24 ), (uint8_t)( n >> 16 ), (uint8_t)( n >> 8 ), (uint8_t)n } );
		bundle.insert( bundle.end(), m->begin(), m->end() );
	}
	messages.clear();
	CHECK( ParseOsc( bundle.data(), bundle.size(), messages ) && messages.size() == 2 );
	CHECK( messages.size() == 2 && messages[ 0 ].values.size() == 2 && messages[ 0 ].values[ 0 ] == 1 && messages[ 0 ].values[ 1 ] == 1 );
	CHECK( messages.size() == 2 && messages[ 1 ].address == "/composition/tempocontroller/tempotap" && messages[ 1 ].values.empty() );

	// Broken packets are refused, not read past their end.
	messages.clear();
	CHECK( !ParseOsc( packet.data(), packet.size() - 4, messages ) );
	CHECK( !ParseOsc( (const uint8_t*)"nope", 4, messages ) );
	std::vector< uint8_t > lying = bundle;
	lying[ 19 ] = 200; // a length past the end
	CHECK( !ParseOsc( lying.data(), lying.size(), messages ) );
}

static void TestMidi()
{
	MidiKey key;
	float value = 0;
	CHECK( ReadMidi( { 0x91, 60, 127 }, key, value ) && key.type == MidiKey::Type::Note && key.channel == 1 && key.number == 60 && value == 1 );
	CHECK( ReadMidi( { 0x91, 60, 0 }, key, value ) && value == 0 ); // note on at velocity 0 = note off
	CHECK( ReadMidi( { 0x80, 60, 64 }, key, value ) && value == 0 );
	CHECK( ReadMidi( { 0xB0, 7, 64 }, key, value ) && key.type == MidiKey::Type::Controller && key.number == 7 && std::fabs( value - 64 / 127.0f ) < 1e-6f );
	CHECK( !ReadMidi( { 0xE0, 0, 64 }, key, value ) ); // pitch bend: not mapped
	CHECK( !ReadMidi( { 0xF8 }, key, value ) );
	CHECK( MidiKeyName( { MidiKey::Type::Note, 0, 60 } ) == "C3 ch 1" );
	CHECK( MidiKeyName( { MidiKey::Type::Controller, 9, 21 } ) == "CC 21 ch 10" );
}

static void TestCueAddresses()
{
	ControlTarget t;
	CHECK( ParseControlAddress( "/composition/layers/2/clips/3/cue/4", t ) && t.kind == ControlTarget::Kind::ClipCue && t.layer == 1 && t.column == 2 && t.slot == 3 );
	CHECK( t.IsButton() );
	CHECK( ParseControlAddress( "/composition/layers/1/cue/8", t ) && t.kind == ControlTarget::Kind::LayerCue && t.layer == 0 && t.slot == 7 );
	CHECK( !ParseControlAddress( "/composition/layers/1/cue/9", t ) );
	CHECK( !ParseControlAddress( "/composition/layers/1/clips/1/cue/0", t ) );
	CHECK( ParseControlAddress( "/composition/layers/3/transition/duration", t ) && t.kind == ControlTarget::Kind::LayerTransition && t.layer == 2 );
	CHECK( !t.IsButton() && !t.IsSwitch() );
}

static void TestLayerHeaderAddresses()
{
	using Kind = ControlTarget::Kind;
	ControlTarget t;
	CHECK( ParseControlAddress( "/composition/layers/2/master", t ) && t.kind == Kind::LayerMaster && t.layer == 1 );
	CHECK( ParseControlAddress( "/composition/layers/2/speed", t ) && t.kind == Kind::LayerSpeed );
	CHECK( ParseControlAddress( "/composition/layers/4/connectnextclip", t ) && t.kind == Kind::LayerNextClip && t.layer == 3 && t.IsButton() );
	CHECK( ParseControlAddress( "/composition/layers/4/connectprevclip", t ) && t.kind == Kind::LayerPrevClip );
	CHECK( ParseControlAddress( "/composition/selectedlayer/connectnextclip", t ) && t.kind == Kind::SelectedNextClip && t.IsButton() );
	CHECK( ParseControlAddress( "/composition/selectedlayer/connectprevclip", t ) && t.kind == Kind::SelectedPrevClip );
	CHECK( ParseControlAddress( "/composition/connectnextcolumn", t ) && t.kind == Kind::NextColumn );
	CHECK( ParseControlAddress( "/composition/decks/2/select", t ) && t.kind == Kind::DeckSelect && t.slot == 1 && t.IsButton() );
	CHECK( ParseControlAddress( "/composition/selectnextdeck", t ) && t.kind == Kind::NextDeck );
	CHECK( ParseControlAddress( "/composition/selectprevdeck", t ) && t.kind == Kind::PrevDeck );
	CHECK( !ParseControlAddress( "/composition/decks/0/select", t ) );
	CHECK( ParseControlAddress( "/timeline/cue/3", t ) && t.kind == Kind::TimelineCue && t.slot == 2 && t.IsButton() );
	CHECK( ParseControlAddress( "/timeline/play", t ) && t.kind == Kind::TimelinePlay && t.IsSwitch() );
	CHECK( ParseControlAddress( "/timeline/nextcue", t ) && t.kind == Kind::TimelineNextCue );
	CHECK( ParseControlAddress( "/timeline/stop", t ) && t.kind == Kind::TimelineStop );
	CHECK( !ParseControlAddress( "/timeline/cue/0", t ) && !ParseControlAddress( "/timeline/rewind", t ) );
	CHECK( ParseControlAddress( "/composition/connectprevcolumn", t ) && t.kind == Kind::PrevColumn );
	CHECK( ParseControlAddress( "/composition/crossfader/phase", t ) && t.kind == Kind::Crossfader );
	CHECK( ParseControlAddress( "/composition/bypassed", t ) && t.kind == Kind::CompositionBypass && t.IsSwitch() );
	CHECK( !ParseControlAddress( "/composition/selectedlayer/clear", t ) );
}

static void TestSettings()
{
	ControlSettings s;
	s.oscEnabled = true;
	s.oscPort    = 9000;
	s.midiInputsOff = { "Launchpad" };
	s.midi.push_back( { { MidiKey::Type::Controller, 2, 14 }, "/composition/master", "" } );
	s.midi.push_back( { { MidiKey::Type::Note, 0, 36 }, "/composition/layers/1/clips/1/connect", "APC mini mk2" } );
	KeyChord chord{ "Q", true, true, false };
	s.keys.push_back( { chord, "/composition/layers/2/clear" } );
	s.keys.push_back( { { "F5" }, "/composition/tempocontroller/tempotap" } );
	ControlSettings loaded;
	CHECK( loaded.Load( s.Save() ) && loaded.midi.size() == 2 && loaded.midi[ 1 ].device == "APC mini mk2" && loaded.midi[ 0 ].device.empty() );
	loaded = ControlSettings{};
	CHECK( loaded.Load( s.Save() ) );
	CHECK( loaded.keys.size() == 2 && loaded.keys[ 0 ].chord == chord && loaded.keys[ 0 ].address == "/composition/layers/2/clear" );
	CHECK( loaded.keys.size() == 2 && loaded.keys[ 1 ].chord == KeyChord{ "F5" } );
	CHECK( KeyChordName( chord ) == "Ctrl+Shift+Q" && KeyChordName( { "Space" } ) == "Space" );
	CHECK( loaded.oscEnabled && loaded.oscPort == 9000 && loaded.midiInputsOff.size() == 1 && loaded.midi.size() == 2 );
	CHECK( loaded.midi.size() == 2 && loaded.midi[ 0 ].key == s.midi[ 0 ].key && loaded.midi[ 1 ].address == s.midi[ 1 ].address );
	CHECK( !loaded.Load( "{}" ) && loaded.oscPort == 9000 );

	// Shortcut presets carry the maps alone: loading one replaces the maps and keeps the OSC and MIDI settings.
	s.preset = "Club";
	ControlSettings other;
	other.oscPort = 7100;
	other.keys.push_back( { { "Z" }, "/composition/stop" } );
	CHECK( other.LoadShortcuts( s.SaveShortcuts() ) );
	CHECK( other.oscPort == 7100 && other.keys.size() == 2 && other.midi.size() == 2 && other.keys[ 1 ].chord == KeyChord{ "F5" } );
	CHECK( other.SaveShortcuts() == s.SaveShortcuts() );
	CHECK( other.LoadShortcuts( s.Save() ) && other.keys.size() == 2 ); // a whole settings file works too
	CHECK( !other.LoadShortcuts( "{\"format\":\"stlt-vj-composition\"}" ) && other.keys.size() == 2 );
	CHECK( loaded.Load( s.Save() ) && loaded.preset == "Club" );
}

// MIDI inputs by name: the number RtMidi adds on Windows goes, so one device listed twice is one input and a
// different USB socket still finds its settings.
static void TestMidiDeviceNames()
{
	CHECK( MidiDeviceName( "APC mini mk2 0", true ) == "APC mini mk2" );
	CHECK( MidiDeviceName( "MIDIIN2 (APC mini mk2) 3", true ) == "MIDIIN2 (APC mini mk2)" );
	CHECK( MidiDeviceName( "APC mini mk2 2", true ) == MidiDeviceName( "APC mini mk2 0", true ) );
	CHECK( MidiDeviceName( "nanoKONTROL2", true ) == "nanoKONTROL2" ); // nothing to take off
	CHECK( MidiDeviceName( "Launchpad X 1A", true ) == "Launchpad X 1A" );
	CHECK( MidiDeviceName( "APC mini mk2 0", false ) == "APC mini mk2 0" ); // other systems name ports themselves
	CHECK( MidiDeviceName( "Midi Through:Midi Through Port-0 14:0", false ) == "Midi Through:Midi Through Port-0 14:0" );

	// Inputs turned off are saved by these names, marked so names saved before (with the number) are converted
	// once on loading, and never again: a device whose own name ends in a number stays as it is.
	ControlSettings s;
	s.midiInputsOff = { "MPK mini 3" };
	std::string saved = s.Save();
	CHECK( saved.find( "\"midiNames\"" ) != std::string::npos );
	ControlSettings back;
	CHECK( back.Load( saved ) && back.midiInputsOff.size() == 1 && back.midiInputsOff[ 0 ] == "MPK mini 3" );
	CHECK( back.Load( "{\"format\":\"stlt-vj-control\",\"midiInputsOff\":[\"APC mini mk2 0\"]}" ) && back.midiInputsOff.size() == 1
	       && back.midiInputsOff[ 0 ] == MidiDeviceName( "APC mini mk2 0" ) );
}

// Controllers' lights: the APC mini mk2's pads by colour and brightness, its buttons on, off or blinking; other
// controllers a note on of 127 or 0; faders none. Outputs found by the inputs' names.
static void TestLights()
{
	using V = std::vector< unsigned char >;
	MidiKey pad{ MidiKey::Type::Note, 0, 12 };
	CHECK( LightMessage( "APC mini mk2", pad, Light::Off ) == V( { 0x90, 12, 0 } ) );
	CHECK( LightMessage( "APC mini mk2", pad, Light::Ready ) == V( { 0x92, 12, 45 } ) );   // blue, half bright
	CHECK( LightMessage( "APC mini mk2", pad, Light::On ) == V( { 0x96, 12, 21 } ) );      // green, full
	CHECK( LightMessage( "APC mini mk2", pad, Light::Waiting ) == V( { 0x99, 12, 21 } ) ); // green, pulsing
	MidiKey track{ MidiKey::Type::Note, 0, 100 }, scene{ MidiKey::Type::Note, 0, 119 };
	CHECK( LightMessage( "APC mini mk2", track, Light::On ) == V( { 0x90, 100, 1 } ) );
	CHECK( LightMessage( "APC mini mk2", scene, Light::Waiting ) == V( { 0x90, 119, 2 } ) );
	CHECK( LightMessage( "APC mini mk2", scene, Light::Ready ) == V( { 0x90, 119, 0 } ) );
	CHECK( LightMessage( "APC mini mk2", MidiKey{ MidiKey::Type::Note, 0, 80 }, Light::On ).empty() ); // no light there
	CHECK( LightMessage( "MIDIIN2 (APC mini mk2)", pad, Light::On ) == V( { 0x96, 12, 21 } ) );
	CHECK( LightMessage( "Launchpad", MidiKey{ MidiKey::Type::Note, 3, 40 }, Light::On ) == V( { 0x93, 40, 127 } ) );
	CHECK( LightMessage( "Launchpad", MidiKey{ MidiKey::Type::Note, 3, 40 }, Light::Ready ) == V( { 0x93, 40, 0 } ) );
	CHECK( LightMessage( "APC mini mk2", MidiKey{ MidiKey::Type::Controller, 0, 48 }, Light::On ).empty() );
	CHECK( MidiOutputNameFor( "MIDIIN2 (APC mini mk2)" ) == "MIDIOUT2 (APC mini mk2)" );
	CHECK( MidiOutputNameFor( "APC mini mk2" ) == "APC mini mk2" );
}

// A MIDI clock: the tempo through the jitter of its ticks, the beat from Start, Stop, Song Position and Continue,
// and the composition's beat following it.
static void TestMidiClock()
{
	const unsigned char tick[] = { 0xF8 }, start[] = { 0xFA }, cont[] = { 0xFB }, stop[] = { 0xFC };
	std::mt19937 random( 7 );
	std::uniform_real_distribution< double > jitter( -0.001, 0.001 ); // a millisecond either way, as USB MIDI does
	double t = 100;
	auto ticks = [ & ]( MidiClock& clock, int n, double bpm ) {
		for( int i = 0; i < n; ++i )
		{
			t += 60.0 / bpm / 24;
			clock.Message( tick, 1, t + jitter( random ) );
		}
	};

	MidiClock clock;
	CHECK( !clock.At( t ).receiving );
	ticks( clock, 96, 128 ); // a song already running when we joined: counted from here, not placed
	MidiClock::State s = clock.At( t );
	CHECK( s.receiving && s.playing && !s.placed );
	CHECK( std::fabs( s.bpm - 128 ) < 0.1 );
	CHECK( std::fabs( s.beat - 4 ) < 0.01 ); // the last tick came a little early or late
	CHECK( !clock.At( t + 1 ).receiving ); // silent for a second: gone

	clock.Message( start, 1, t );
	int jumps = clock.At( t ).jumps;
	CHECK( clock.At( t ).placed && clock.At( t ).beat == 0 ); // waiting for the first tick
	ticks( clock, 1, 128 );
	CHECK( clock.At( t ).beat < 0.01 ); // the first tick after Start is the top
	ticks( clock, 48, 128 );
	s = clock.At( t );
	CHECK( std::fabs( s.beat - 2 ) < 0.01 );
	// Between ticks the beat moves with time, but never past the next tick.
	CHECK( clock.At( t + 0.01 ).beat > clock.At( t ).beat );
	CHECK( clock.At( t + 0.2 ).beat <= 2 + 1.0 / 24 + 1e-9 );

	// A new tempo shows within half a beat or so.
	ticks( clock, 18, 140 );
	CHECK( std::fabs( clock.At( t ).bpm - 140 ) < 0.5 );

	// Stop: the position holds while the ticks go on; Song Position then Continue: from there.
	clock.Message( stop, 1, t );
	double held = clock.At( t ).beat;
	ticks( clock, 24, 140 );
	CHECK( !clock.At( t ).playing && std::fabs( clock.At( t ).beat - held ) < 0.05 );
	const unsigned char spp[] = { 0xF2, 64 & 0x7F, 0 }; // 64 sixteenths: beat 16
	clock.Message( spp, 3, t );
	clock.Message( cont, 1, t );
	CHECK( clock.At( t ).jumps != jumps );
	ticks( clock, 1, 140 );
	CHECK( std::fabs( clock.At( t ).beat - 16 ) < 0.01 );

	// The composition's beat and tempo follow it; a Start sends the beat back to the top.
	TempoSync sync;
	sync.SetClockInput( "Mixer" );
	sync.OnMidi( "Other", start, 1, t ); // another input's clock: not followed
	ticks( sync.clock, 1, 120 );
	double bpm = 100, beat = 37.5;
	CHECK( !sync.Update( t, bpm, beat ) );
	CHECK( sync.Following() == TempoSync::Source::Internal && bpm == 100 ); // one tick: no tempo yet
	sync.OnMidi( "Mixer", start, 1, t );
	for( int i = 0; i < 24; ++i )
	{
		t += 60.0 / 120 / 24;
		sync.OnMidi( "Mixer", tick, 1, t );
	}
	CHECK( sync.Update( t, bpm, beat ) ); // went back: what waited for a beat should go
	CHECK( sync.Following() == TempoSync::Source::Clock );
	CHECK( std::fabs( bpm - 120 ) < 0.1 && std::fabs( beat - 23.0 / 24 ) < 1e-6 );
	CHECK( sync.Status().find( "120.0 BPM" ) != std::string::npos );
	// Resync: beat 0 now, and on from there with the clock.
	sync.Resync( t, beat );
	CHECK( beat == 0 );
	for( int i = 0; i < 12; ++i )
	{
		t += 60.0 / 120 / 24;
		sync.OnMidi( "Mixer", tick, 1, t );
	}
	CHECK( !sync.Update( t, bpm, beat ) && std::fabs( beat - 0.5 ) < 1e-6 );
	// The clock goes quiet: the composition's own tempo and beat take over again.
	CHECK( !sync.Update( t + 2, bpm, beat ) && sync.Following() == TempoSync::Source::Internal );
	sync.SetClockInput( "" );
	sync.OnMidi( "Mixer", tick, 1, t + 3 );
	CHECK( !sync.Update( t + 3, bpm, beat ) && sync.Following() == TempoSync::Source::Internal );
}

// Ableton Link, alone: the composition's tempo and beat go in, the session's come back; a BPM changed here goes to
// the session. Two of them on this computer find each other when the network allows it.
static void TestLink()
{
	if( !TempoSync::LinkBuiltIn() )
		return;
	TempoSync a;
	a.EnableLink( true, 130, 6.25 );
	CHECK( a.LinkEnabled() );
	double bpm = 130, beat = 6.25;
	a.Update( TempoSync::Now(), bpm, beat );
	CHECK( a.Following() == TempoSync::Source::Link );
	CHECK( std::fabs( bpm - 130 ) < 1e-3 && std::fabs( beat - 6.25 ) < 0.05 );
	bpm = 140; // typed, tapped or mapped here
	a.Update( TempoSync::Now(), bpm, beat );
	CHECK( std::fabs( bpm - 140 ) < 1e-3 );
	double before = beat;
	a.Update( TempoSync::Now() + 0.1, bpm, beat );
	CHECK( beat >= before && std::fabs( bpm - 140 ) < 1e-3 );
	a.Resync( TempoSync::Now(), beat );
	CHECK( std::fabs( beat ) < 0.05 ); // alone: at once

	TempoSync b;
	b.EnableLink( true, 120, 0 );
	for( int i = 0; i < 40 && a.LinkPeers() == 0; ++i )
		std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
	std::printf( "Link: %s\n", a.LinkPeers() > 0 ? "two sessions on this computer found each other" : "no other session found (no network for it here)" );
	if( a.LinkPeers() > 0 )
	{
		double bpmA = 140, beatA = 0, bpmB = 120, beatB = 0;
		for( int i = 0; i < 40 && ( i < 4 || std::fabs( bpmA - bpmB ) > 1e-3 ); ++i )
		{
			a.Update( TempoSync::Now(), bpmA, beatA );
			b.Update( TempoSync::Now(), bpmB, beatB );
			std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
		}
		CHECK( std::fabs( bpmA - bpmB ) < 1e-3 ); // one tempo for both (Link keeps it as microseconds a beat)
		CHECK( std::fabs( std::fmod( beatA - beatB + 400, 4.0 ) ) < 0.05 || std::fabs( std::fmod( beatA - beatB + 400, 4.0 ) - 4 ) < 0.05 ); // the same bar
		bpmA = 97;
		a.Update( TempoSync::Now(), bpmA, beatA );
		for( int i = 0; i < 40 && std::fabs( bpmB - 97 ) > 1e-3; ++i )
		{
			std::this_thread::sleep_for( std::chrono::milliseconds( 50 ) );
			b.Update( TempoSync::Now(), bpmB, beatB );
		}
		CHECK( std::fabs( bpmB - 97 ) < 1e-3 );
	}
	a.EnableLink( false, 0, 0 );
	CHECK( !a.LinkEnabled() && a.LinkPeers() == 0 );
}

// LTC as a timecode generator plays it: 80 bits a frame, biphase mark, the sync word at the end.
static std::vector< float > MakeLtc( Timecode start, int frames, int rate, float amplitude, float noise, bool inverted )
{
	std::vector< float > out;
	std::mt19937 random( 3 );
	std::uniform_real_distribution< float > hiss( -noise, noise );
	double fps = TimecodeRateFps( start.rate ), perBit = rate / ( fps * 80 ), at = 0;
	float level = inverted ? -1.0f : 1.0f;
	Timecode tc = start;
	int nominal = TimecodeRateFrames( tc.rate );
	for( int f = 0; f < frames; ++f )
	{
		uint8_t bits[ 80 ] = {};
		auto put = [ & ]( int first, int count, int value ) {
			for( int i = 0; i < count; ++i )
				bits[ first + i ] = ( value >> i ) & 1;
		};
		put( 0, 4, tc.frames % 10 );
		put( 8, 2, tc.frames / 10 );
		bits[ 10 ] = tc.rate == TimecodeRate::Fps2997Drop;
		put( 16, 4, tc.seconds % 10 );
		put( 24, 3, tc.seconds / 10 );
		put( 32, 4, tc.minutes % 10 );
		put( 40, 3, tc.minutes / 10 );
		put( 48, 4, tc.hours % 10 );
		put( 56, 2, tc.hours / 10 );
		const uint8_t sync[ 16 ] = { 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1 };
		for( int i = 0; i < 16; ++i )
			bits[ 64 + i ] = sync[ i ];
		for( int b = 0; b < 80; ++b )
		{
			double end = at + perBit, middle = at + perBit / 2;
			level      = -level; // every cell starts with an edge
			while( (double)out.size() < end )
			{
				if( bits[ b ] && (double)out.size() >= middle && (double)out.size() - 1 < middle )
					level = -level; // a 1 has another in the middle
				out.push_back( amplitude * level + hiss( random ) );
			}
			at = end;
		}
		// The next frame, drop frame numbering included.
		if( ++tc.frames >= nominal )
		{
			tc.frames = 0;
			if( ++tc.seconds >= 60 )
			{
				tc.seconds = 0;
				if( ++tc.minutes >= 60 )
				{
					tc.minutes = 0;
					tc.hours   = ( tc.hours + 1 ) % 24;
				}
				if( tc.rate == TimecodeRate::Fps2997Drop && tc.minutes % 10 != 0 )
					tc.frames = 2;
			}
		}
	}
	return out;
}

static void TestTimecode()
{
	// The time of a timecode, drop frame counted out.
	Timecode tc;
	CHECK( ParseTimecode( "01:00:00:00", tc ) && tc.hours == 1 && std::fabs( tc.Seconds() - 3600 ) < 1e-9 );
	tc.rate = TimecodeRate::Fps25;
	CHECK( ParseTimecode( "00:00:10:12", tc ) && std::fabs( tc.Seconds() - 10.48 ) < 1e-9 );
	CHECK( ParseTimecode( "00:10:00;00", tc ) && tc.rate == TimecodeRate::Fps2997Drop );
	CHECK( std::fabs( tc.Seconds() - 600 ) < 0.001 ); // drop frame keeps to the clock every ten minutes
	CHECK( tc.Text() == "00:10:00;00" );
	CHECK( !ParseTimecode( "1:2:3", tc ) && !ParseTimecode( "00:61:00:00", tc ) && !ParseTimecode( "aa:00:00:00", tc ) );

	// The receiver: running while the time moves on, extrapolated between times, kept going for the freewheel.
	TimecodeReceiver receiver;
	CHECK( !receiver.At( 0, 0.5 ).present );
	Timecode t0;
	t0.rate    = TimecodeRate::Fps25;
	t0.seconds = 10;
	receiver.Receive( t0, 100 );
	CHECK( receiver.At( 100, 0.5 ).present && !receiver.At( 100, 0.5 ).running ); // one time: not yet moving
	Timecode t1 = t0;
	t1.frames   = 1;
	receiver.Receive( t1, 100.04 );
	TimecodeReceiver::State st = receiver.At( 100.06, 0.5 );
	CHECK( st.running && std::fabs( st.seconds - 10.06 ) < 1e-9 );
	CHECK( receiver.At( 100.5, 0.5 ).running && !receiver.At( 100.6, 0.5 ).running ); // freewheel, then stopped
	CHECK( !receiver.At( 100.2, 0 ).running ); // no freewheel: three frames
	receiver.Receive( t1, 100.08 );
	receiver.Receive( t1, 100.12 );
	CHECK( receiver.At( 100.13, 0.5 ).running ); // a frame or two the same: a late packet, perhaps
	receiver.Receive( t1, 100.16 ); // the same time for more than two frames and a half: stopped
	CHECK( !receiver.At( 100.17, 0.5 ).running && std::fabs( receiver.At( 100.13, 0.5 ).seconds - 10.04 ) < 1e-9 );

	// LTC read back, at several rates, sample rates, levels, with noise and either polarity.
	struct Case
	{
		const char* start;
		TimecodeRate rate;
		int sampleRate;
		float amplitude, noise;
		bool inverted;
	};
	const Case cases[] = {
		{ "01:00:00:00", TimecodeRate::Fps25, 48000, 0.5f, 0.0f, false },
		{ "00:59:59:20", TimecodeRate::Fps30, 44100, 0.05f, 0.01f, true },
		{ "10:23:45:12", TimecodeRate::Fps24, 96000, 0.3f, 0.05f, false },
		{ "00:00:59;25", TimecodeRate::Fps2997Drop, 48000, 0.2f, 0.02f, false },
	};
	for( const Case& c : cases )
	{
		Timecode start;
		start.rate = c.rate;
		CHECK( ParseTimecode( c.start, start ) );
		start.rate                = c.rate;
		std::vector< float > ltc  = MakeLtc( start, 50, c.sampleRate, c.amplitude, c.noise, c.inverted );
		TimecodeReceiver got;
		LtcDecoder decoder( got );
		double t = 0;
		for( size_t at = 0; at < ltc.size(); at += 256 ) // in blocks, as a sound card hands them
		{
			size_t n = std::min< size_t >( 256, ltc.size() - at );
			t        = (double)( at + n ) / c.sampleRate;
			decoder.Feed( ltc.data() + at, n, c.sampleRate, t );
		}
		CHECK( decoder.Frames() >= 48 ); // the first word or two go to finding the bits
		TimecodeReceiver::State st = got.At( t, 0.5 );
		CHECK( st.present && st.running && st.last.rate == c.rate );
		// At the end of 50 frames the time is the start's plus 50 frames.
		double expected = start.Seconds() + 50 / TimecodeRateFps( c.rate );
		if( std::fabs( st.seconds - expected ) > 0.002 )
			std::fprintf( stderr, "LTC %s: %.4f, expected %.4f\n", c.start, st.seconds, expected );
		CHECK( std::fabs( st.seconds - expected ) < 0.002 );
	}

	// MTC: quarter frames make a time every two frames, behind by a frame and three quarters; full frames locate.
	TimecodeReceiver mtcGot;
	MtcDecoder mtc( mtcGot );
	auto quarter = [ & ]( int h, int m, int s, int f, int rateBits, double time, double perQuarter ) {
		int values[ 8 ] = { f & 15, f >> 4, s & 15, s >> 4, m & 15, m >> 4, h & 15, ( h >> 4 ) | rateBits << 1 };
		for( int piece = 0; piece < 8; ++piece )
		{
			unsigned char message[] = { 0xF1, (unsigned char)( piece << 4 | values[ piece ] ) };
			mtc.Message( message, 2, time + piece * perQuarter );
		}
	};
	double q = 1.0 / 25 / 4;
	quarter( 1, 2, 3, 4, 1, 50, q );
	quarter( 1, 2, 3, 6, 1, 50 + 8 * q, q );
	st = mtcGot.At( 50 + 15 * q, 0.5 );
	Timecode six;
	six.rate = TimecodeRate::Fps25;
	CHECK( ParseTimecode( "01:02:03:06", six ) );
	CHECK( st.running && st.last.rate == TimecodeRate::Fps25 && std::fabs( st.seconds - ( six.Seconds() + 1.75 / 25 ) ) < 1e-9 );
	unsigned char full[] = { 0xF0, 0x7F, 0x7F, 0x01, 0x01, (unsigned char)( 3 << 5 | 2 ), 30, 0, 15, 0xF7 };
	mtc.Message( full, sizeof( full ), 60 );
	st = mtcGot.At( 60.5, 0.5 );
	CHECK( st.present && !st.running && st.last.rate == TimecodeRate::Fps30 && st.last.Text() == "02:30:00:15" );

	// Art-Net.
	uint8_t packet[ 19 ] = { 'A', 'r', 't', '-', 'N', 'e', 't', 0, 0x00, 0x97, 0, 14, 0, 0, 24, 59, 7, 1, 2 };
	Timecode art;
	CHECK( ParseArtTimecode( packet, sizeof( packet ), art ) && art.Text() == "01:07:59;24" && art.rate == TimecodeRate::Fps2997Drop );
	packet[ 9 ] = 0x50; // ArtDmx
	CHECK( !ParseArtTimecode( packet, sizeof( packet ), art ) );
	CHECK( !ParseArtTimecode( packet, 12, art ) );
}

int main()
{
	TestTimecode();
	TestLink();
	TestMidiClock();
	TestLights();
	TestMidiDeviceNames();
	TestAddresses();
	TestOsc();
	TestMidi();
	TestCueAddresses();
	TestLayerHeaderAddresses();
	TestSettings();
	if( failures )
	{
		std::fprintf( stderr, "%d check(s) failed\n", failures );
		return 1;
	}
	std::printf( "control tests passed\n" );
	return 0;
}
