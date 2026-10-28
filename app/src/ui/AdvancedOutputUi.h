#pragma once

namespace stltvj
{
class App;

// The Advanced Output editor: screens and their slices on the left, the canvas in the middle
// (Input Selection: what each slice takes from the composition; Output Transformation: where and how it lands
// on the screen), the selected screen's or slice's settings on the right.
void DrawAdvancedOutputPanel( App& app );
// Opens the editor on a tab (0 input, 1 output) with a slice selected.
void ShowAdvancedOutputTab( int tab, int screen, int slice );
}
