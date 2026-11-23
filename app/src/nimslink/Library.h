#pragma once
#include <string>
#include <vector>
#include "nimslink/Protocol.h"

// The tracks the DJs' decks have had, remembered from one night to the next, so that clips can be linked to them without
// the DJ playing them again (the Track Links panel). Kept in the settings folder as JSON.
namespace nimslink
{
struct LibraryTrack
{
	TrackId id;
	double bpm = 0;
	std::string key;
	double seen = 0; // when a deck last had it (seconds since 1970)
};

class TrackLibrary
{
public:
	// A track a deck has now: added, or its details and time brought up to date. True when the list changed in a way
	// worth saving (a new track, new details; not only the time).
	bool Remember( const TrackInfo& track, double now );
	// Takes a track off the list.
	void Forget( const TrackId& id );
	const LibraryTrack* Find( const TrackId& id ) const;
	const std::vector< LibraryTrack >& Tracks() const { return tracks; }

	std::string Save() const;
	// False (and the list left empty) when the text is not a track list.
	bool Load( const std::string& text );

private:
	std::vector< LibraryTrack > tracks;
};
}
