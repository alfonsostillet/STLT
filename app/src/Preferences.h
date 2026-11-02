#pragma once
#include <string>
#include "Composition.h"

namespace stltvj
{
// What the Preferences window sets, kept in preferences.json in the settings folder. MIDI and OSC live in the
// control settings, the plugin folders in their own file.
struct Preferences
{
	// General.
	bool reopenLastSession = true; // the last composition comes back at start
	int startView          = 0;    // 0: the view in use when the app closed, 1: Session, 2: Arrangement

	// Audio.
	std::string audioDevice; // empty: the system's default
	int audioBuffer = 256;   // frames per buffer
	std::string audioInput;  // what animations listen to (Audio modes); empty: the system's default input
	float audioInputGain = 1; // amplification of that input before it is measured

	// Sync: the tempo and beat from outside.
	bool linkEnabled = false;   // Ableton Link
	std::string midiClockInput; // the MIDI input whose clock is followed: "" none, "*" any
	float syncLatencyMs = 0;    // the beat is taken this much ahead, for a picture that shows late
	int timecodeSource  = 0;    // TimecodeInput::Source: 0 off, 1 LTC, 2 MTC, 3 Art-Net
	std::string timecodeLtcDevice; // the sound card input with LTC ("": the system's default)
	int timecodeLtcChannel = 0;    // its channel, from 0
	std::string timecodeMtcInput = "*"; // the MIDI input with MTC ("*": any)
	float timecodeFreewheelMs = 500;    // the timeline runs on this long when the timecode stops arriving

	// Video.
	bool hardwareDecoding = true;
	int frameCacheMegabytes = 256; // per clip that is not HAP: decoded frames kept for scratching back over them

	// Defaults for clips and layers made from now on (those already there keep theirs), as in Resolume.
	FitMode clipFit             = FitMode::Fit;
	PlayMode clipPlayMode       = PlayMode::Loop;
	bool clipBpmSync            = false; // new clips last a number of beats
	float clipSyncBeats         = 16;
	TriggerStyle clipTrigger    = TriggerStyle::Normal;
	BlendMode layerBlend        = BlendMode::Alpha;
	TransitionMode layerTransition = TransitionMode::Fade;
	float layerTransitionTime   = 0; // seconds; 0: cut

	std::string Save() const;
	bool Load( const std::string& json );
	// The clip defaults on `clip` (a new one; its BPM sync needs its length, so the caller has opened it).
	void ApplyToClip( Clip& clip ) const;
};
}
