#pragma once
#include <string>
#include "Gl.h"

namespace stltvj
{
// A linked GLSL program. Programs are shared between contexts, so one instance serves every window.
class Program
{
public:
	Program() = default;
	~Program();
	Program( const Program& ) = delete;
	Program& operator=( const Program& ) = delete;

	// Builds from GLSL 4.10 sources (the #version line is added); false with the log in error().
	bool Build( const char* vertex, const char* fragment );
	const std::string& Error() const { return error; }

	void Use() const { glUseProgram( id ); }
	GLint Uniform( const char* name ) const { return glGetUniformLocation( id, name ); }
	GLuint Id() const { return id; }

private:
	GLuint id = 0;
	std::string error;
};

// The vertex shader every 2D pass uses: a quad from gl_VertexID (draw 4 vertices as a triangle strip),
// placed by `uTransform` (quad space -1..1 to clip space) with texture coordinates 0..1, v = 0 at the top.
extern const char* kQuadVertexShader;
}
