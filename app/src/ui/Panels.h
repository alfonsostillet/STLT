#pragma once

namespace stltvj
{
class App;

// Each panel is an ImGui window drawn by one of these; the dock space decides where it sits.
void DrawMainMenu( App& app );
void DrawToolbar( App& app );
void DrawClipGrid( App& app );
void DrawLayersPanel( App& app );
void DrawCompositionMonitor( App& app );
void DrawPreviewMonitor( App& app );
void DrawCompositionPanel( App& app );
void DrawLayerPanel( App& app );
void DrawClipPanel( App& app );
void DrawFilesPanel( App& app );
void DrawOutputPanel( App& app );
void DrawPerformancePanel( App& app );
void DrawStatusBar( App& app );

// Small drawn icons (no icon font needed).
enum class Icon
{
	Play,
	Pause,
	Stop,
	Reverse,
	Close,
	Previous, // |<
	Next,     // >|
};
bool IconButton( const char* id, Icon icon, bool active = false, float size = 0 );
}
