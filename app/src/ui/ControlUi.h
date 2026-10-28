#pragma once
#include <string>

namespace stltvj
{
class App;

// The Control panel: OSC port, MIDI inputs, mappings and learning.
void DrawControlPanel( App& app );

// While learning, the panels with mappable controls are drawn inactive (a click picks a control instead of using
// it) and each mappable control calls MapTarget right after its widget: it is highlighted, shows its mapping,
// and a click makes it the target of the next MIDI message.
void BeginMappable( App& app );
void EndMappable( App& app );
void MapTarget( App& app, const std::string& address );
}
