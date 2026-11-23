#include "nimslink/Hand.h"

namespace nimslink
{
bool LayerHand::Touched()
{
	bool had = held;
	held     = false;
	return had;
}

bool LayerHand::Step( const TrackId* best, bool playsBest, bool active )
{
	if( best )
	{
		held  = true;
		track = *best;
		return !playsBest;
	}
	// No linked track plays: what NIMS put stays (holding its frame) as long as the layer plays it.
	held = held && active;
	return false;
}
}
