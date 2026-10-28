#pragma once
#include "gl/Gl.h"

namespace stltvj
{
// An RGBA8 render target. Framebuffer objects belong to the context that made them: use it on the main context only.
class Fbo
{
public:
	Fbo() = default;
	~Fbo();
	Fbo( const Fbo& ) = delete;
	Fbo& operator=( const Fbo& ) = delete;

	// (Re)allocates when the size changes; true if the target is ready.
	bool Ensure( int width, int height );
	void Release();
	// Binds as the draw target with a matching viewport.
	void Bind() const;

	GLuint Texture() const { return texture; }
	GLuint Framebuffer() const { return framebuffer; }
	int Width() const { return width; }
	int Height() const { return height; }

private:
	GLuint framebuffer = 0;
	GLuint texture     = 0;
	int width          = 0;
	int height         = 0;
};
}
