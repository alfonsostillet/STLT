#pragma once
#include <string>
#include <vector>
#include "gl/Gl.h"

struct GLFWwindow;
struct GLFWmonitor;

namespace stltvj
{
class Compositor;

// The window the audience sees: fullscreen on a chosen display, or a movable window for rehearsals.
// It shares textures and programs with the main context but has its own vertex array.
class OutputWindow
{
public:
	struct Display
	{
		std::string name;        // "2: DELL U2720Q", for menus
		std::string monitorName; // as the system reports it: what a screen setup stores
		int width = 0, height = 0, refresh = 0;
		GLFWmonitor* monitor = nullptr;
	};
	static std::vector< Display > Displays();
	static GLFWmonitor* FindDisplay( const std::string& monitorName );

	~OutputWindow();
	// monitor null: a window of windowWidth x windowHeight. Re-opening moves the output.
	bool Open( GLFWwindow* shareWith, GLFWmonitor* monitor, const std::string& title = "STLT-VJ Output", int windowWidth = 1280, int windowHeight = 720,
	           bool vsync = true );
	void Close();
	bool IsOpen() const { return window != nullptr; }
	bool CloseRequested() const;
	std::string Description() const { return description; }

	// Draws the composition texture and presents (vsynced). Leaves `restore`'s context current.
	void Present( Compositor& compositor, GLuint texture, int width, int height, GLFWwindow* restore );

private:
	GLFWwindow* window = nullptr;
	GLuint vao         = 0;
	std::string description;
};
}
