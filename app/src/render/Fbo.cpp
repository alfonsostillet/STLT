#include "Fbo.h"

namespace stltvj
{
Fbo::~Fbo()
{
	Release();
}

void Fbo::Release()
{
	if( framebuffer )
		glDeleteFramebuffers( 1, &framebuffer );
	if( texture )
		glDeleteTextures( 1, &texture );
	framebuffer = texture = 0;
	width = height = 0;
}

bool Fbo::Ensure( int w, int h )
{
	if( w <= 0 || h <= 0 )
		return false;
	if( framebuffer && w == width && h == height )
		return true;
	Release();
	glGenTextures( 1, &texture );
	glBindTexture( GL_TEXTURE_2D, texture );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glGenFramebuffers( 1, &framebuffer );
	glBindFramebuffer( GL_FRAMEBUFFER, framebuffer );
	glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0 );
	bool complete = glCheckFramebufferStatus( GL_FRAMEBUFFER ) == GL_FRAMEBUFFER_COMPLETE;
	glClearColor( 0, 0, 0, 0 );
	glClear( GL_COLOR_BUFFER_BIT );
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	if( !complete )
	{
		Release();
		return false;
	}
	width  = w;
	height = h;
	return true;
}

void Fbo::Bind() const
{
	glBindFramebuffer( GL_FRAMEBUFFER, framebuffer );
	glViewport( 0, 0, width, height );
}
}
