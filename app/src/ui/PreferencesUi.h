#pragma once

namespace stltvj
{
class App;

// The Preferences window (Edit > Preferences, Ctrl+,), laid out as Resolume's: sections at the left (General,
// Audio, Video, MIDI, OSC, Defaults), the chosen one at the right. Changes are kept at once.
void DrawPreferences( App& app );
}
