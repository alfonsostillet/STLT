#pragma once

namespace stltvj
{
class App;
struct AdvancedOutputSetup;

// The Advanced Output's third step, Stage: the space in metres (surfaces and blocks) and the projectors in it
// where they really stand, in a 3D view (orbit, top, front, or through a projector's lens), with what each
// projector lights, how sharp and bright, and where projectors overlap. The surfaces and projectors on the left,
// the selected one's settings on the right.
void DrawStageStep( App& app, AdvancedOutputSetup& setup, float height );
// The 3D view alone, in a window of its own (the step's "Detach" button); false once that window is closed.
bool DrawStageWindow( App& app, AdvancedOutputSetup& setup );
}
