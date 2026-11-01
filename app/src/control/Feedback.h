#pragma once
#include <string>
#include <vector>
#include "control/Control.h"

namespace stltvj
{
// Lights on MIDI controllers: what a mapped pad or button shows, sent back to the controller as Resolume does, so
// the grid on an APC mini is the grid on screen.
enum class Light
{
	Off,     // nothing there (an empty cell, a switch that is off)
	Ready,   // something to launch (a clip waiting in its cell)
	On,      // playing, or switched on
	Waiting, // launched, starting on the next beat or bar
};

// The message that sets the light of `key` on `device` (an input name, as MidiDeviceName gives it); empty when the
// key has no light this knows how to set (a fader).
//   Akai APC mini mk2: the 8x8 pads take a colour and a brightness (playing green, ready STLT-VJ's blue at half,
//   waiting a pulsing green); the track and scene buttons are on, off or blinking.
//   Other controllers: a note on of 127 lights the key, 0 turns it off.
std::vector< unsigned char > LightMessage( const std::string& device, const MidiKey& key, Light light );

// The output that belongs to an input: the same name, or Windows' "MIDIOUT2 (...)" for "MIDIIN2 (...)".
std::string MidiOutputNameFor( const std::string& inputName );
bool IsApcMiniMk2( const std::string& device );
}
