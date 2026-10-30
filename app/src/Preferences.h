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

	// Video.
	bool hardwareDecoding = true;

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
