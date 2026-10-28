#pragma once
#include <memory>
#include <string>
#include <vector>
#include "gl/Program.h"
#include "output/AdvancedOutput.h"
#include "render/Fbo.h"
#include "render/OutputWindow.h"

struct GLFWwindow;

namespace stltvj
{
class Compositor;

// Renders the advanced output: for each screen, every slice cuts its input area out of the composition and
// draws it, warped, onto the screen; then the screen's colour settings apply and its window or display shows it.
//
// A slice is drawn as a fine triangle mesh whose vertices follow the warp (perspective, grid or curved grid),
// so straight and bent shapes alike come out smooth; soft edges are computed per pixel.
class OutputRenderer
{
public:
	bool Init( std::string& error );
	void Shutdown();

	// Main context. `composition` is an FBO texture (first row at the bottom) of compWidth x compHeight.
	void Render( const AdvancedOutputSetup& setup, GLuint composition, int compWidth, int compHeight );
	// The finished picture of screen `index` (FBO texture), or 0.
	GLuint ScreenTexture( size_t index ) const;

	// Opens, moves or closes output windows so each screen shows on its device. Main context current.
	void SyncWindows( const AdvancedOutputSetup& setup, GLFWwindow* main );
	void CloseWindows();
	// Shows every open output (after the main window's swap). Leaves `main`'s context current.
	void Present( Compositor& compositor, GLFWwindow* main );
	bool AnyWindowOpen() const;
	// Screen state for the panels: "Display DELL 1920x1080", "Window", "not connected", "Virtual".
	std::string DeviceStatus( size_t index ) const;

private:
	struct ScreenState
	{
		Fbo target;
		Fbo finished;
		std::unique_ptr< OutputWindow > window;
		OutputDevice device = OutputDevice::Virtual;
		std::string display;
		bool missingDisplay = false;
		int width = 0, height = 0;
	};
	void DrawSlice( const Slice& slice, int index, const Screen& screen, int compWidth, int compHeight );

	void Match( size_t count );
	std::vector< std::unique_ptr< ScreenState > > screens; // Fbo cannot move: kept by pointer
	Program sliceProgram;
	Program finishProgram;
	GLuint vao = 0, vbo = 0;
	std::vector< float > vertices;
};
}
