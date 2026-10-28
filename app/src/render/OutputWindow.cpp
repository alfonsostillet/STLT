#include "OutputWindow.h"
#include <GLFW/glfw3.h>
#include "render/Compositor.h"

namespace stltvj
{
std::vector< OutputWindow::Display > OutputWindow::Displays()
{
	std::vector< Display > displays;
	int count              = 0;
	GLFWmonitor** monitors = glfwGetMonitors( &count );
	for( int i = 0; i < count; ++i )
	{
		Display d;
		d.monitor               = monitors[ i ];
		const GLFWvidmode* mode = glfwGetVideoMode( monitors[ i ] );
		if( mode )
		{
			d.width   = mode->width;
			d.height  = mode->height;
			d.refresh = mode->refreshRate;
		}
		const char* name = glfwGetMonitorName( monitors[ i ] );
		d.monitorName    = name ? name : "Display";
		d.name           = std::to_string( i + 1 ) + ": " + d.monitorName;
		displays.push_back( d );
	}
	return displays;
}

OutputWindow::~OutputWindow()
{
	Close();
}

GLFWmonitor* OutputWindow::FindDisplay( const std::string& monitorName )
{
	for( const Display& d : Displays() )
		if( d.monitorName == monitorName )
			return d.monitor;
	return nullptr;
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
