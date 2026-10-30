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
		std::string name;        // "2: DELL U2720Q (main)", for menus
		std::string monitorName; // as the system reports it ("Generic PnP Monitor" for many displays on Windows)
		// What a setup stores to find the display again: the name alone is not enough, as several displays often
		// share it, so its place on the desktop and its number among the displays of that name come with it.
		std::string id;
		int width = 0, height = 0, refresh = 0;
		int x = 0, y = 0; // top left on the desktop
		bool primary = false;
		GLFWmonitor* monitor = nullptr;
	};
	static std::vector< Display > Displays();
	// The display a stored id names: the same one; else one of that name at the same place, or the same
	// number among those of that name; an id from before (a name alone): the first of that name.
	static GLFWmonitor* FindDisplay( const std::string& id );
	// The same search in a given list (an index, -1: none); and an id for a display of that list.
	static int MatchDisplay( const std::vector< Display >& displays, const std::string& id );
	static void SetIds( std::vector< Display >& displays );
	// A stored id for people: its display's menu name, or its name with "(not connected)".
	static std::string DisplayLabel( const std::string& id );

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
