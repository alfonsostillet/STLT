#pragma once
#include <string>

namespace stltvj
{
class App;

// The Shortcuts panel: keyboard and MIDI mappings, MIDI inputs, the OSC port, and mapping mode.
void DrawControlPanel( App& app );
// The MIDI inputs (on/off each) and the OSC receiver's settings; true when something changed (the caller saves
// and reconfigures).
bool DrawMidiInputs( App& app );
bool DrawOscSettings( App& app );

// While learning, the panels with mappable controls are drawn inactive (a click picks a control instead of using
// it) and each mappable control calls MapTarget right after its widget: it is highlighted, shows its mapping,
// and a click makes it the target of the next key or MIDI message.
void BeginMappable( App& app );
void EndMappable( App& app );
void MapTarget( App& app, const std::string& address );
}
