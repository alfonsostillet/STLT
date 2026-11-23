#pragma once
#include "nimslink/Protocol.h"

// Whether Traktor has a layer: NIMS PRO LINK, when it is on (the NIMS switch, like Ableton Link's), launches on a layer
// the clip linked to the track that plays. Pioneer's Pro DJ Link in Resolume locks such a layer; here a clip launched or
// stopped by hand on it turns NIMS off instead (App::SetNimsActive), so the VJ is never locked out, and NIMS on again
// brings back every linked clip at its track's place. docs/NIMSLINK.md tells the whole story.
namespace nimslink
{
struct LayerHand
{
	bool held = false; // the clip on the layer was put there (or found there) by NIMS PRO LINK
	TrackId track;     // its track

	// The VJ launched or stopped a clip on the layer (a click, a key, MIDI, OSC, a column). True when NIMS had the layer:
	// then NIMS goes off.
	bool Touched();

	// Once a frame while NIMS is on. `best`: the track of the linked clip that should play on the layer (playing, its
	// fader up, the loudest), null when none; `playsBest`: the layer plays that clip already; `active`: the layer plays
	// something. True when NIMS should launch that clip now.
	bool Step( const TrackId* best, bool playsBest, bool active );
};
}
