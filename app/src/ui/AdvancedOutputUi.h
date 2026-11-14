#pragma once
#include <imgui.h>
#include <string>


namespace stltvj
{
class App;

// The Advanced Output editor, as a path in three steps: 1 Screens (the screens STLT sends out, where each goes:
// a display, a window, NDI or sharing; this computer's displays as the system arranges them), 2 Map (each slice's
// part of the picture and its place and shape on the screen, side by side, with the problems found) and 3 Stage
// (the space in metres and the projectors in it: ui/StageUi.h). Which setup is edited (the Grid's or the Timeline's) and whether it is on show are along
// the top, saving along the bottom.
void DrawAdvancedOutputPanel( App& app );
// Asks whether to save the Advanced Output setup when its editor closes, or the app quits, with unsaved edits.
void DrawAdvancedOutputSavePrompt( App& app );
// Opens the editor on a tab (0 input, 1 output) with a slice selected.
void ShowAdvancedOutputTab( int tab, int screen, int slice );
// The colour that marks a picture wherever it is chosen (timeline tracks, slices): "" the composition, else a
// canvas of the setup, by its place in the list.
ImU32 PictureColour( const App& app, const std::string& source );
// Its name: "Composition" or the canvas's.
std::string PictureName( const App& app, const std::string& source );
// Sends a timeline track to one screen of the timeline's setup (`screen` its index), or to every screen (-1: the
// composition). A screen that gets its first track shows its own picture from then on; one left without tracks
// goes back to the composition.
void SendTrackTo( App& app, int track, int screen );
// The screen of the timeline's setup a track goes to (-1: every screen).
int TrackScreen( const App& app, int track );
// The colour of the screen at `index` (the timeline's chips, the monitor's labels).
ImU32 ScreenColour( int index );
}
