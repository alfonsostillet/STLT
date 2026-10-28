#include "Program.h"

namespace stltvj
{
const char* kQuadVertexShader = R"(
uniform mat3 uTransform;
uniform vec4 uTexRect; // u0, v0, u1, v1
out vec2 vUv;
void main()
{
	vec2 corner = vec2( ( gl_VertexID & 1 ) == 0 ? -1.0 : 1.0, ( gl_VertexID & 2 ) == 0 ? -1.0 : 1.0 );
	vec2 uv = vec2( corner.x < 0.0 ? uTexRect.x : uTexRect.z, corner.y > 0.0 ? uTexRect.y : uTexRect.w );
	vUv = uv;
	vec3 p = uTransform * vec3( corner, 1.0 );
	gl_Position = vec4( p.xy, 0.0, 1.0 );
}
)";

static GLuint Compile( GLenum type, const char* source, std::string& error )
{
	const char* sources[ 2 ] = { "#version 410 core\n", source };
	GLuint shader            = glCreateShader( type );
	glShaderSource( shader, 2, sources, nullptr );
	glCompileShader( shader );
	GLint ok = 0;
	glGetShaderiv( shader, GL_COMPILE_STATUS, &ok );
	if( !ok )
	{
		char log[ 2048 ] = {};
		glGetShaderInfoLog( shader, sizeof( log ), nullptr, log );
		error = std::string( type == GL_VERTEX_SHADER ? "vertex: " : "fragment: " ) + log;
		glDeleteShader( shader );
		return 0;
	}
	return shader;
}

Program::~Program()
{
	if( id )
		glDeleteProgram( id );
}

bool Program::Build( const char* vertex, const char* fragment )
{
	error.clear();
	GLuint vs = Compile( GL_VERTEX_SHADER, vertex, error );
	if( !vs )
		return false;
	GLuint fs = Compile( GL_FRAGMENT_SHADER, fragment, error );
	if( !fs )
	{
		glDeleteShader( vs );
		return false;
	}
	GLuint program = glCreateProgram();
	glAttachShader( program, vs );
	glAttachShader( program, fs );
	glLinkProgram( program );
	glDeleteShader( vs );
	glDeleteShader( fs );
	GLint ok = 0;
	glGetProgramiv( program, GL_LINK_STATUS, &ok );
	if( !ok )
	{
		char log[ 2048 ] = {};
		glGetProgramInfoLog( program, sizeof( log ), nullptr, log );
		error = std::string( "link: " ) + log;
		glDeleteProgram( program );
		return false;
	}
	if( id )
		glDeleteProgram( id );
	id = program;
	return true;
}
}
