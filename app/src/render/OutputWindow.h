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
		std::string name;
		int width = 0, height = 0, refresh = 0;
		GLFWmonitor* monitor = nullptr;
	};
	static std::vector< Display > Displays();

	~OutputWindow();
	// monitor null: a 1280x720 window. Re-opening moves the output.
	bool Open( GLFWwindow* shareWith, GLFWmonitor* monitor );
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
