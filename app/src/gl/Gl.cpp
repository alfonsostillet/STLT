#include "Gl.h"
#include <GLFW/glfw3.h>
#include <cstring>

namespace stltvj
{
#define STLTVJ_DEFINE_GL( type, name ) type name = nullptr;
STLTVJ_GL_FUNCTIONS( STLTVJ_DEFINE_GL )
#undef STLTVJ_DEFINE_GL

static GlCaps caps;

static bool HasExtension( const char* name )
{
	GLint count = 0;
	glGetIntegerv( GL_NUM_EXTENSIONS, &count );
	for( GLint i = 0; i < count; ++i )
	{
		const char* extension = (const char*)glGetStringi( GL_EXTENSIONS, (GLuint)i );
		if( extension && !std::strcmp( extension, name ) )
			return true;
	}
	return false;
}

bool LoadGl( const char** missing )
{
#define STLTVJ_LOAD_GL( type, name )                                  \
	name = (type)glfwGetProcAddress( #name );                         \
	if( !name )                                                       \
	{                                                                 \
		if( missing )                                                 \
			*missing = #name;                                         \
		return false;                                                 \
	}
	STLTVJ_GL_FUNCTIONS( STLTVJ_LOAD_GL )
#undef STLTVJ_LOAD_GL

	GLint major = 0, minor = 0;
	glGetIntegerv( GL_MAJOR_VERSION, &major );
	glGetIntegerv( GL_MINOR_VERSION, &minor );
	caps.s3tc     = HasExtension( "GL_EXT_texture_compression_s3tc" ) || HasExtension( "GL_S3_s3tc" );
	caps.bptc     = major > 4 || ( major == 4 && minor >= 2 ) || HasExtension( "GL_ARB_texture_compression_bptc" );
	caps.renderer = (const char*)glGetString( GL_RENDERER );
	caps.version  = (const char*)glGetString( GL_VERSION );
	return true;
}

const GlCaps& Caps()
{
	return caps;
}
}
