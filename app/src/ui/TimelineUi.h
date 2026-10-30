#pragma once

namespace stltvj
{
class App;

// The Arrangement view's timeline: transport and cues, the ruler (bars and timecode), blocks, tracks of clips
// placed in time, and an inspector for the clip or block selected.
void DrawTimelinePanel( App& app );
}
