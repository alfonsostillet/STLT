#pragma once
#include <deque>
#include <string>
#include <vector>
#include "Composition.h"
#include "render/Compositor.h"
#include "render/Fbo.h"
#include "render/OutputWindow.h"
#include "ui/Workspaces.h"

struct GLFWwindow;

namespace stltvj
{
struct LaunchOptions
{
	std::string capturePath; // write the window to this .bmp after `captureFrames` frames, then quit
	int captureFrames = 60;
	struct ClipArg
	{
		int layer, column;
		std::string path;
	};
	std::vector< ClipArg > clips;     // --clip layer,column,path (1-based, launched right away)
	bool fresh = false;               // ignore the last session
	int width = 0, height = 0;        // window size override
};

class App
{
public:
	bool Init( const LaunchOptions& options );
	void Run();
	void Shutdown();

	// Actions shared by the panels and shortcuts.
	void LoadClip( int layer, int column, const std::string& path );
	void RemoveClip( int layer, int column );
	void MoveClip( int fromLayer, int fromColumn, int toLayer, int toColumn );
	void Launch( int layer, int column );
	void LaunchColumn( int column );
	void ClearLayer( int layer );
	void Select( int layer, int column );
	void NewComposition();
	bool SaveCompositionTo( const std::string& path );
	bool OpenComposition( const std::string& path );
	void SetStatus( const std::string& message, bool error = false );
	void TapTempo();

	// Panel state.
	Composition composition;
	Compositor compositor;
	OutputWindow output;
	Workspaces workspaces;
	PanelVisibility panels;
	Fbo preview;
	GLFWwindow* window = nullptr;

	std::string compositionPath;
	std::string browserDirectory;
	std::vector< std::string > droppedPaths; // from the OS, placed by the clip grid on the next frame
	float dropX = 0, dropY = 0;              // where they were dropped, in screen coordinates
	std::string status;
	bool statusIsError     = false;
	double statusTime      = 0;
	std::deque< float > frameTimes;   // ms, for the performance panel
	std::vector< double > taps;
	std::string configDir;
	bool quitRequested = false;

private:
	void Frame();
	void UpdatePlayback( double dt );
	void DrawUi();
	void Capture();
	ClipPlayer* PlayerAt( int layer, int column );

	LaunchOptions options;
	double lastTime = 0;
	int frameCount  = 0;
	bool outputVsync = false;
};
}
