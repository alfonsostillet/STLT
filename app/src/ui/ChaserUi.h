#pragma once

namespace stltvj
{
class App;

// The chaser's editor (Output > Chaser), as STLT-CHASER's: sequences at the left, the stage with the Advanced
// Output's slices (click or sweep to light them in the step), the steps below, the Factory and the playback
// settings (driver, steps per beat, sustain, release, echoes).
void DrawChaserPanel( App& app );
}
