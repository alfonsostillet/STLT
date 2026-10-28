#pragma once
// OpenGL 4.1 core entry points, loaded at runtime through GLFW.
// The pointers live in namespace stltvj so they never clash with the system OpenGL library's own symbols.
#include <GL/glcorearb.h>

#ifndef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F0
#endif
#ifndef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
#define GL_COMPRESSED_RGBA_S3TC_DXT5_EXT 0x83F3
#endif

#define STLTVJ_GL_FUNCTIONS( X )                                         \
	X( PFNGLGETSTRINGPROC, glGetString )                                 \
	X( PFNGLGETSTRINGIPROC, glGetStringi )                               \
	X( PFNGLGETINTEGERVPROC, glGetIntegerv )                             \
	X( PFNGLGETERRORPROC, glGetError )                                   \
	X( PFNGLENABLEPROC, glEnable )                                       \
	X( PFNGLDISABLEPROC, glDisable )                                     \
	X( PFNGLBLENDFUNCPROC, glBlendFunc )                                 \
	X( PFNGLBLENDCOLORPROC, glBlendColor )                               \
	X( PFNGLBLENDEQUATIONPROC, glBlendEquation )                         \
	X( PFNGLCOLORMASKPROC, glColorMask )                                 \
	X( PFNGLDEPTHMASKPROC, glDepthMask )                                 \
	X( PFNGLCLEARPROC, glClear )                                         \
	X( PFNGLCLEARCOLORPROC, glClearColor )                               \
	X( PFNGLVIEWPORTPROC, glViewport )                                   \
	X( PFNGLFLUSHPROC, glFlush )                                         \
	X( PFNGLFINISHPROC, glFinish )                                       \
	X( PFNGLGENTEXTURESPROC, glGenTextures )                             \
	X( PFNGLDELETETEXTURESPROC, glDeleteTextures )                       \
	X( PFNGLBINDTEXTUREPROC, glBindTexture )                             \
	X( PFNGLACTIVETEXTUREPROC, glActiveTexture )                         \
	X( PFNGLTEXPARAMETERIPROC, glTexParameteri )                         \
	X( PFNGLTEXIMAGE2DPROC, glTexImage2D )                               \
	X( PFNGLTEXSUBIMAGE2DPROC, glTexSubImage2D )                         \
	X( PFNGLCOMPRESSEDTEXIMAGE2DPROC, glCompressedTexImage2D )           \
	X( PFNGLCOMPRESSEDTEXSUBIMAGE2DPROC, glCompressedTexSubImage2D )     \
	X( PFNGLPIXELSTOREIPROC, glPixelStorei )                             \
	X( PFNGLREADPIXELSPROC, glReadPixels )                               \
	X( PFNGLGENBUFFERSPROC, glGenBuffers )                               \
	X( PFNGLDELETEBUFFERSPROC, glDeleteBuffers )                         \
	X( PFNGLBINDBUFFERPROC, glBindBuffer )                               \
	X( PFNGLBUFFERDATAPROC, glBufferData )                               \
	X( PFNGLMAPBUFFERRANGEPROC, glMapBufferRange )                       \
	X( PFNGLUNMAPBUFFERPROC, glUnmapBuffer )                             \
	X( PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers )                     \
	X( PFNGLDELETEFRAMEBUFFERSPROC, glDeleteFramebuffers )               \
	X( PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer )                     \
	X( PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D )           \
	X( PFNGLCHECKFRAMEBUFFERSTATUSPROC, glCheckFramebufferStatus )       \
	X( PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer )                     \
	X( PFNGLCREATESHADERPROC, glCreateShader )                           \
	X( PFNGLSHADERSOURCEPROC, glShaderSource )                           \
	X( PFNGLCOMPILESHADERPROC, glCompileShader )                         \
	X( PFNGLGETSHADERIVPROC, glGetShaderiv )                             \
	X( PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog )                   \
	X( PFNGLDELETESHADERPROC, glDeleteShader )                           \
	X( PFNGLCREATEPROGRAMPROC, glCreateProgram )                         \
	X( PFNGLATTACHSHADERPROC, glAttachShader )                           \
	X( PFNGLLINKPROGRAMPROC, glLinkProgram )                             \
	X( PFNGLGETPROGRAMIVPROC, glGetProgramiv )                           \
	X( PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog )                 \
	X( PFNGLDELETEPROGRAMPROC, glDeleteProgram )                         \
	X( PFNGLUSEPROGRAMPROC, glUseProgram )                               \
	X( PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation )               \
	X( PFNGLUNIFORM1IPROC, glUniform1i )                                 \
	X( PFNGLUNIFORM1FPROC, glUniform1f )                                 \
	X( PFNGLUNIFORM2FPROC, glUniform2f )                                 \
	X( PFNGLUNIFORM3FPROC, glUniform3f )                                 \
	X( PFNGLUNIFORM4FPROC, glUniform4f )                                 \
	X( PFNGLUNIFORMMATRIX3FVPROC, glUniformMatrix3fv )                   \
	X( PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays )                     \
	X( PFNGLDELETEVERTEXARRAYSPROC, glDeleteVertexArrays )               \
	X( PFNGLBINDVERTEXARRAYPROC, glBindVertexArray )                     \
	X( PFNGLDRAWARRAYSPROC, glDrawArrays )

namespace stltvj
{
#define STLTVJ_DECLARE_GL( type, name ) extern type name;
STLTVJ_GL_FUNCTIONS( STLTVJ_DECLARE_GL )
#undef STLTVJ_DECLARE_GL

// Loads every entry point from the current context; false names the first missing one in `missing`.
bool LoadGl( const char** missing );

// What the current context can do beyond the 4.1 core.
struct GlCaps
{
	bool s3tc = false; // DXT1/DXT5: HAP, HAP Alpha, HAP Q
	bool bptc = false; // BC7/BC6H: HAP R, HAP HDR
	const char* renderer = "";
	const char* version  = "";
};
const GlCaps& Caps();
}
