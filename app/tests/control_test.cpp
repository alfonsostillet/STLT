// Tests for remote control: the address space, OSC packets, MIDI messages and the saved settings.
#include <cmath>
#include <cstdio>
#include <cstring>
#include "control/Control.h"
#include "control/RemoteInputs.h"

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
	s.midi.push_back( { { MidiKey::Type::Controller, 2, 14 }, "/composition/master" } );
	s.midi.push_back( { { MidiKey::Type::Note, 0, 36 }, "/composition/layers/1/clips/1/connect" } );
	KeyChord chord{ "Q", true, true, false };
	s.keys.push_back( { chord, "/composition/layers/2/clear" } );
	s.keys.push_back( { { "F5" }, "/composition/tempocontroller/tempotap" } );
	ControlSettings loaded;
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

int main()
{
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
