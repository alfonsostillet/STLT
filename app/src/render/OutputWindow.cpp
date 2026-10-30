#include "OutputWindow.h"
#include <GLFW/glfw3.h>
#include <cstdio>
#include "render/Compositor.h"

namespace stltvj
{
std::vector< OutputWindow::Display > OutputWindow::Displays()
{
	std::vector< Display > displays;
	int count              = 0;
	GLFWmonitor** monitors = glfwGetMonitors( &count );
	GLFWmonitor* primary   = glfwGetPrimaryMonitor();
	for( int i = 0; i < count; ++i )
	{
		Display d;
		d.monitor               = monitors[ i ];
		d.primary               = monitors[ i ] == primary;
		const GLFWvidmode* mode = glfwGetVideoMode( monitors[ i ] );
		if( mode )
		{
			d.width   = mode->width;
			d.height  = mode->height;
			d.refresh = mode->refreshRate;
		}
		glfwGetMonitorPos( monitors[ i ], &d.x, &d.y );
		const char* name = glfwGetMonitorName( monitors[ i ] );
		d.monitorName    = name ? name : "Display";
		displays.push_back( d );
	}
	SetIds( displays );
	return displays;
}

void OutputWindow::SetIds( std::vector< Display >& displays )
{
	for( size_t i = 0; i < displays.size(); ++i )
	{
		Display& d = displays[ i ];
		int same   = 1;
		for( size_t j = 0; j < i; ++j )
			same += displays[ j ].monitorName == d.monitorName ? 1 : 0;
		d.id   = d.monitorName + "#" + std::to_string( same ) + "@" + std::to_string( d.x ) + "," + std::to_string( d.y );
		d.name = std::to_string( i + 1 ) + ": " + d.monitorName + ( d.primary ? " (main)" : "" );
	}
}

namespace
{
// An id's parts: the name, the number among displays of that name and the place (false: a name alone).
bool SplitId( const std::string& id, std::string& name, int& number, int& x, int& y )
{
	size_t at = id.rfind( '@' ), hash = id.rfind( '#', at );
	if( at == std::string::npos || hash == std::string::npos )
		return false;
	name = id.substr( 0, hash );
	return std::sscanf( id.c_str() + hash + 1, "%d", &number ) == 1 && std::sscanf( id.c_str() + at + 1, "%d,%d", &x, &y ) == 2;
}
}

int OutputWindow::MatchDisplay( const std::vector< Display >& displays, const std::string& id )
{
	for( size_t i = 0; i < displays.size(); ++i )
		if( displays[ i ].id == id )
			return (int)i;
	std::string name;
	int number = 0, x = 0, y = 0;
	if( !SplitId( id, name, number, x, y ) )
	{
		for( size_t i = 0; i < displays.size(); ++i )
			if( displays[ i ].monitorName == id )
				return (int)i;
		return -1;
	}
	for( size_t i = 0; i < displays.size(); ++i )
		if( displays[ i ].monitorName == name && displays[ i ].x == x && displays[ i ].y == y )
			return (int)i;
	for( size_t i = 0; i < displays.size(); ++i )
	{
		std::string otherName;
		int otherNumber = 0, ox = 0, oy = 0;
		if( SplitId( displays[ i ].id, otherName, otherNumber, ox, oy ) && otherName == name && otherNumber == number )
			return (int)i;
	}
	return -1;
}

GLFWmonitor* OutputWindow::FindDisplay( const std::string& id )
{
	std::vector< Display > displays = Displays();
	int found                       = MatchDisplay( displays, id );
	return found >= 0 ? displays[ found ].monitor : nullptr;
}

std::string OutputWindow::DisplayLabel( const std::string& id )
{
	std::vector< Display > displays = Displays();
	int found                       = MatchDisplay( displays, id );
	if( found >= 0 )
		return displays[ found ].name;
	std::string name;
	int number = 0, x = 0, y = 0;
	return ( SplitId( id, name, number, x, y ) ? name : id ) + " (not connected)";
}

OutputWindow::~OutputWindow()
{
	Close();
}

bool OutputWindow::Open( GLFWwindow* shareWith, GLFWmonitor* monitor, const std::string& title, int windowWidth, int windowHeight, bool vsync )
{
	Close();
	glfwWindowHint( GLFW_CONTEXT_VERSION_MAJOR, 4 );
	glfwWindowHint( GLFW_CONTEXT_VERSION_MINOR, 1 );
	glfwWindowHint( GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE );
	glfwWindowHint( GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE );
	glfwWindowHint( GLFW_AUTO_ICONIFY, GLFW_FALSE ); // keep the show running when focus moves to the control screen
	glfwWindowHint( GLFW_FOCUS_ON_SHOW, GLFW_FALSE );
	if( monitor )
	{
		const GLFWvidmode* mode = glfwGetVideoMode( monitor );
		glfwWindowHint( GLFW_REFRESH_RATE, mode->refreshRate );
		window      = glfwCreateWindow( mode->width, mode->height, title.c_str(), monitor, shareWith );
		description = std::string( glfwGetMonitorName( monitor ) ? glfwGetMonitorName( monitor ) : "Display" ) + " " + std::to_string( mode->width ) + "x"
		              + std::to_string( mode->height );
	}
	else
	{
		window      = glfwCreateWindow( windowWidth, windowHeight, title.c_str(), nullptr, shareWith );
		description = "Window";
	}
	glfwDefaultWindowHints();
	if( !window )
		return false;
	// Escape on the output (it only has the keyboard once clicked, or when it covers the interface's display)
	// closes it, like its close button.
	glfwSetKeyCallback( window, []( GLFWwindow* w, int key, int, int action, int ) {
		if( key == GLFW_KEY_ESCAPE && action == GLFW_PRESS )
			glfwSetWindowShouldClose( w, GLFW_TRUE );
	} );
	glfwMakeContextCurrent( window );
	glfwSwapInterval( vsync ? 1 : 0 );
	glGenVertexArrays( 1, &vao );
	glfwMakeContextCurrent( shareWith );
	return true;
}

void OutputWindow::Close()
{
	if( !window )
		return;
	GLFWwindow* previous = glfwGetCurrentContext();
	glfwMakeContextCurrent( window );
	glDeleteVertexArrays( 1, &vao );
	vao = 0;
	glfwMakeContextCurrent( previous == window ? nullptr : previous );
	glfwDestroyWindow( window );
	window = nullptr;
}

bool OutputWindow::CloseRequested() const
{
	return window && glfwWindowShouldClose( window );
}

void OutputWindow::Present( Compositor& compositor, GLuint texture, int width, int height, GLFWwindow* restore )
{
	if( !window )
		return;
	glfwMakeContextCurrent( window );
	int fbWidth = 0, fbHeight = 0;
	glfwGetFramebufferSize( window, &fbWidth, &fbHeight );
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	glViewport( 0, 0, fbWidth, fbHeight );
	glClearColor( 0, 0, 0, 1 );
	glClear( GL_COLOR_BUFFER_BIT );
	glBindVertexArray( vao );
	compositor.DrawFitted( texture, width, height, fbWidth, fbHeight, 1.0f );
	glBindVertexArray( 0 );
	glfwSwapBuffers( window );
	glfwMakeContextCurrent( restore );
}
}
