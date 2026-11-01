#pragma once
#include <string>

namespace stltvj
{
// STLT-CHASER with STLT-VJ: the editor (an app of its own, shipped in the chaser folder next to STLT-VJ) draws
// the step sequences on the Advanced Output's slices; the effects STLT-CHASER, STLT-WIPER and STLT-OUTLINER
// (shipped in the plugins folder) play them on the composition, a layer or a clip.
//
// STLT-VJ writes its slices to chaser-stage.json for the editor, and both the editor and the effects keep the
// sequences in the chaser folder of STLT-VJ's settings, apart from a Resolume setup's: STLT_DATA_DIR, set
// for this process (the effects) and the editor it opens.
namespace chaser
{
// The folder for the sequences (made if needed), set as STLT_DATA_DIR. Call before loading the effects.
std::string UseDataDirectory( const std::string& configDir );
// Where the editor is: STLTVJ_CHASER_EDITOR, else the chaser folder next to STLT-VJ (or in the .app's Resources).
std::string EditorPath();
// Opens the editor on `stagePath` (or brings the open one forward); false with `error` when it cannot start.
bool OpenEditor( const std::string& stagePath, std::string& error );
// Collects editors that have closed (Linux and macOS); call now and then.
void Reap();
}
}
