#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace stltvj
{
// Remote control: OSC messages and MIDI notes and controllers drive the composition through one address space,
// modelled on Resolume's OSC (layers, columns and effects counted from 1):
//
//   /composition/layers/L/clips/C/connect            launch a clip
//   /composition/columns/C/connect                   launch a column
//   /composition/layers/L/clear                      stop a layer
//   /composition/layers/L/bypassed                   bypass on or off
//   /composition/layers/L/solo                       solo on or off
//   /composition/layers/L/video/opacity              0..1
//   /composition/layers/L/audio/volume               0..1, and /composition/layers/L/clips/C/audio/volume
//   /composition/layers/L/clips/C/cue/N              jump to cue point N (1..8), launching the clip if needed
//   /composition/layers/L/cue/N                      jump to cue point N of the clip playing on the layer
//   /composition/layers/L/transition/duration        0..1 over 0..10 seconds (or 0..16 beats)
//   /composition/layers/L/master                     0..1, video and sound together
//   /composition/layers/L/speed                      0..1 over 0..4
//   /composition/layers/L/connectnextclip            the next clip of the layer (and connectprevclip)
//   /composition/selectedlayer/connectnextclip       the same on the selected layer (and connectprevclip)
//   /composition/connectnextcolumn                   the next column (and connectprevcolumn)
//   /composition/crossfader/phase                    0 (group A) .. 1 (group B)
//   /composition/bypassed                            the whole output black, on or off
//   /composition/audio/volume                        master volume, 0..1
//   /composition/master (or /composition/video/opacity)  0..1
//   /composition/speed                               0..1 over 0..4
//   /composition/tempocontroller/tempo               0..1 over 20..500 BPM, or the BPM itself
//   /composition/tempocontroller/tempotap
//   /composition/tempocontroller/resync
//   /composition/pause                               pause on or off
//   /composition/stop                                stop every layer
//   [scope]/video/effects/S/params/NAME              an effect parameter, 0..1 over its range; the scope is
//                                                    /composition, /composition/layers/L or
//                                                    /composition/layers/L/clips/C
//
// Values are 0..1. Buttons (connect, clear, tap...) act when the value goes above 0; switches (bypassed, solo,
// pause) follow the value, or flip when a message has none.
struct ControlTarget
{
	enum class Kind
	{
		None,
		ClipConnect,
		ColumnConnect,
		LayerClear,
		LayerBypass,
		LayerSolo,
		LayerOpacity,
		LayerVolume,
		ClipVolume,
		ClipCue,
		LayerCue,
		LayerTransition,
		LayerMaster,
		LayerSpeed,
		LayerNextClip,
		LayerPrevClip,
		SelectedNextClip,
		SelectedPrevClip,
		NextColumn,
		PrevColumn,
		Crossfader,
		CompositionBypass,
		AudioVolume,
		Master,
		Speed,
		Tempo,
		TempoTap,
		Resync,
		Pause,
		Stop,
		EffectParam,
	};
	Kind kind  = Kind::None;
	int layer  = -1; // 0-based
	int column = -1; // 0-based
	int slot   = -1; // effect position in its chain, or cue point, 0-based
	std::string param;
	// Effect scope: the composition (layer < 0), a layer (column < 0) or a clip.

	bool IsButton() const;
	bool IsSwitch() const;
};

bool ParseControlAddress( const std::string& address, ControlTarget& target );
// An effect parameter's name as it appears in addresses: lower case, spaces and symbols as underscores.
std::string AddressName( const std::string& name );
// The address of an effect parameter: `scope` as in the list above.
std::string EffectParamAddress( const std::string& scope, int slot, const std::string& paramName );

// OSC (opensoundcontrol.org 1.0): messages, alone or in bundles, with float, integer, true/false and no arguments.
struct OscMessage
{
	std::string address;
	std::vector< float > values;
};
bool ParseOsc( const uint8_t* data, size_t size, std::vector< OscMessage >& out );
std::vector< uint8_t > MakeOscMessage( const std::string& address, const std::vector< float >& values );

// MIDI: what a controller sends, and the key a mapping is learnt on.
struct MidiKey
{
	enum class Type
	{
		Note,
		Controller,
	};
	Type type   = Type::Note;
	int channel = 0; // 0..15
	int number  = 0; // note or controller number
	bool operator==( const MidiKey& other ) const { return type == other.type && channel == other.channel && number == other.number; }
};
// A note on, note off or controller change: its key and value (0..1). False for other messages.
bool ReadMidi( const std::vector< unsigned char >& message, MidiKey& key, float& value );
std::string MidiKeyName( const MidiKey& key ); // "C3 ch 1", "CC 7 ch 1"

struct MidiMapping
{
	MidiKey key;
	std::string address;
};

// A key of the computer's keyboard, with the modifiers held with it. `key` is the key's name as the interface
// library gives it ("Q", "F5", "Space", "Keypad1"...).
struct KeyChord
{
	std::string key;
	bool ctrl = false, shift = false, alt = false;
	bool operator==( const KeyChord& other ) const { return key == other.key && ctrl == other.ctrl && shift == other.shift && alt == other.alt; }
};
std::string KeyChordName( const KeyChord& chord ); // "Ctrl+Shift+Q"

struct KeyMapping
{
	KeyChord chord;
	std::string address;
};

// What the Shortcuts panel sets: the keyboard and MIDI mappings and the inputs in use, saved apart from compositions.
struct ControlSettings
{
	bool oscEnabled = false;
	int oscPort     = 7000;
	bool midiEnabled = true;
	std::vector< std::string > midiInputsOff; // input names the user switched off (new devices are on)
	std::vector< MidiMapping > midi;
	std::vector< KeyMapping > keys;

	std::string Save() const;
	bool Load( const std::string& json );
};
}
