#pragma once
#include <cstdint>
#include <imgui.h>

namespace stltvj
{
class App;
struct Clip;

// The Traktor panel (NIMS PRO LINK): the decks of the DJs' Traktor, filled by themselves. A deck dragged onto a clip of
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
// The Track Links panel: every track the decks have had (remembered between sessions) and the tracks clips are linked
// to, each with its clip; a track dragged onto a clip links them, the X unlinks, without the DJ playing anything.
void DrawTrackLinksPanel( App& app );
// What a track dragged out of the Track Links panel carries: its title and artist (UTF-8, ended by a 0) and length.
constexpr const char* kTrackPayload = "STLT_TRAKTOR_TRACK";
struct TrackDrag
{
	char title[ 1024 ];
	char artist[ 1024 ];
	double length;
};
// A track dropped on a clip: the clip is linked to it (and any other clip it had is unlinked).
bool LinkClipToDraggedTrack( App& app, Clip& clip, const void* data, int size );
// The NIMS switch (the toolbar's, the Traktor panel's): NIMS PRO LINK driving the linked clips, or not (App::nimsActive).
void DrawNimsSwitch( App& app );
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
