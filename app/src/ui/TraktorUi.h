#pragma once
#include <cstdint>
#include <imgui.h>

namespace stltvj
{
class App;
struct Clip;

// The Traktor panel (NI LINK): the decks of the DJs' Traktor, filled by themselves. A deck dragged onto a clip of
// the grid links the clip to the deck's track: from then on the clip plays on its layer when the track plays.
void DrawTraktorPanel( App& app );

// What a deck dragged out of the panel carries.
constexpr const char* kDeckPayload = "STLT_TRAKTOR_DECK";
struct DeckRef
{
	uint32_t source; // the transmitter's id
	int deck;
};
// A deck dropped on a clip: the clip is linked to the deck's track. False when the deck has no track any more.
bool LinkClipToDeck( App& app, Clip& clip, const DeckRef& deck );
// Traktor's colours for decks A to D.
ImU32 DeckColour( int deck );
// On a clip's picture, top right before `right`: its deck's letter when its track is loaded, a link sign otherwise.
// Returns the width taken.
float DrawTrackBadge( App& app, const Clip& clip, ImDrawList* draw, ImVec2 topRight );
// In a clip's right-click menu: link to a track loaded now, or unlink.
void TrackMenuItems( App& app, Clip& clip );
// In the Clip panel: the linked track, where it plays, the offset, Unlink.
void TrackSection( App& app, Clip& clip );
}
