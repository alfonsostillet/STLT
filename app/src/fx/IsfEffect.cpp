#include "fx/IsfEffect.h"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <map>
#include "gl/Program.h"
#include "render/Fbo.h"

namespace stltvj
{
namespace
{
// The pipeline's images are premultiplied; ISF shaders expect straight colour. In and out of it:
const char* kStraighten = R"(
uniform sampler2D uInput;
in vec2 vUv;
out vec4 fragColor;
void main()
{
	vec4 c = texture( uInput, vUv );
	fragColor = c.a > 0.0 ? vec4( c.rgb / c.a, c.a ) : vec4( 0.0 );
}
)";
const char* kPremultiply = R"(
uniform sampler2D uInput;
in vec2 vUv;
out vec4 fragColor;
void main()
{
	vec4 c = texture( uInput, vUv );
	float a = clamp( c.a, 0.0, 1.0 );
	fragColor = vec4( c.rgb * a, a );
}
)";

struct Compiled
{
	Program program;
	bool failed = false;
	std::map< std::string, GLint > uniforms;
	GLint Uniform( const std::string& name )
	{
		auto it = uniforms.find( name );
		if( it == uniforms.end() )
			it = uniforms.emplace( name, glGetUniformLocation( program.Id(), name.c_str() ) ).first;
		return it->second;
	}
};

std::map< const IsfShader*, std::unique_ptr< Compiled > >& Programs()
{
	static std::map< const IsfShader*, std::unique_ptr< Compiled > > programs;
	return programs;
}

struct Helpers
{
	Program straighten, premultiply;
	GLuint vao = 0;
	bool ok    = false;
};
std::unique_ptr< Helpers >& SharedSlot()
{
	static std::unique_ptr< Helpers > helpers;
	return helpers;
}
Helpers& Shared()
{
	std::unique_ptr< Helpers >& helpers = SharedSlot();
	if( !helpers )
	{
		helpers     = std::make_unique< Helpers >();
		helpers->ok = helpers->straighten.Build( kQuadVertexShader, kStraighten ) && helpers->premultiply.Build( kQuadVertexShader, kPremultiply );
		glGenVertexArrays( 1, &helpers->vao );
	}
	return *helpers;
}

// A full-frame quad through one of the helper programs.
void DrawThrough( const Program& program, GLuint texture )
{
	program.Use();
	const float identity[ 9 ] = { 1, 0, 0, 0, 1, 0, 0, 0, 1 };
	glUniformMatrix3fv( program.Uniform( "uTransform" ), 1, GL_FALSE, identity );
	glUniform4f( program.Uniform( "uTexRect" ), 0, 1, 1, 0 );
	glUniform1i( program.Uniform( "uInput" ), 0 );
	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, texture );
	glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
}

Compiled* CompiledFor( const IsfShader& shader, std::string& error )
{
	std::unique_ptr< Compiled >& c = Programs()[ &shader ];
	if( !c )
	{
		c = std::make_unique< Compiled >();
		if( !c->program.Build( shader.vertex.c_str(), shader.fragment.c_str() ) )
			c->failed = true;
	}
	if( c->failed )
	{
		error = c->program.Error();
		return nullptr;
	}
	return c.get();
}

// A pass's target: two textures, one read while the other is drawn (a pass may read its own target).
struct Target
{
	GLuint framebuffer[ 2 ] = {}, texture[ 2 ] = {};
	int width = 0, height = 0, front = 0;
	bool floating = false;

	Target() = default;
	Target( const Target& ) = delete;
	Target& operator=( const Target& ) = delete;
	~Target() { Release(); }
	void Release()
	{
		glDeleteFramebuffers( 2, framebuffer );
		glDeleteTextures( 2, texture );
		framebuffer[ 0 ] = framebuffer[ 1 ] = texture[ 0 ] = texture[ 1 ] = 0;
		width = height = 0;
	}
	bool Ensure( int w, int h, bool isFloat )
	{
		if( texture[ 0 ] && w == width && h == height && isFloat == floating )
			return true;
		Release();
		floating = isFloat;
		glGenTextures( 2, texture );
		glGenFramebuffers( 2, framebuffer );
		for( int i = 0; i < 2; ++i )
		{
			glBindTexture( GL_TEXTURE_2D, texture[ i ] );
			glTexImage2D( GL_TEXTURE_2D, 0, isFloat ? GL_RGBA16F : GL_RGBA8, w, h, 0, GL_RGBA, isFloat ? GL_FLOAT : GL_UNSIGNED_BYTE, nullptr );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
			glBindFramebuffer( GL_FRAMEBUFFER, framebuffer[ i ] );
			glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture[ i ], 0 );
			if( glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE )
			{
				Release();
				return false;
			}
			glClearColor( 0, 0, 0, 0 );
			glClear( GL_COLOR_BUFFER_BIT );
		}
		width  = w;
		height = h;
		front  = 0;
		return true;
	}
};

class IsfEffect : public Effect
{
public:
	IsfEffect( std::shared_ptr< const IsfShader > s, std::string effectId ) : shader( std::move( s ) ), id( std::move( effectId ) )
	{
		for( const IsfInput& in : shader->inputs )
		{
			first.push_back( (int)params.size() );
			ParamInfo p;
			p.name = in.name;
			p.displayName = in.label.empty() ? in.name : in.label;
			switch( in.type )
			{
			case IsfInput::Type::Event: p.kind = ParamKind::Event; break;
			case IsfInput::Type::Bool:
				p.kind         = ParamKind::Bool;
				p.defaultValue = in.defaults[ 0 ] != 0 ? 1.0f : 0.0f;
				break;
			case IsfInput::Type::Long:
				p.min          = in.min;
				p.max          = in.max;
				p.defaultValue = in.defaults[ 0 ];
				if( !in.values.empty() )
				{
					p.kind    = ParamKind::Option;
					p.options = in.labels;
					for( int v : in.values )
						p.optionValues.push_back( (float)v );
				}
				else
					p.kind = ParamKind::Integer;
				break;
			case IsfInput::Type::Float:
				p.kind         = ParamKind::Float;
				p.min          = std::min( in.min, in.max );
				p.max          = std::max( in.min, in.max );
				p.defaultValue = std::clamp( in.defaults[ 0 ], p.min, p.max );
				break;
			case IsfInput::Type::Point2D:
			{
				ParamInfo x = p, y = p;
				x.name = in.name + " X";
				y.name = in.name + " Y";
				x.displayName = p.displayName + " X";
				y.displayName = p.displayName + " Y";
				x.kind = ParamKind::XPos;
				y.kind = ParamKind::YPos;
				x.min = in.min, x.max = in.max, y.min = in.minY, y.max = in.maxY;
				x.defaultValue = std::clamp( in.defaults[ 0 ], x.min, x.max );
				y.defaultValue = std::clamp( in.defaults[ 1 ], y.min, y.max );
				params.push_back( x );
				params.push_back( y );
				continue;
			}
			case IsfInput::Type::Color:
			{
				static const char* channels[] = { "Red", "Green", "Blue", "Alpha" };
				static const ParamKind kinds[] = { ParamKind::Red, ParamKind::Green, ParamKind::Blue, ParamKind::Alpha };
				for( int k = 0; k < 4; ++k )
				{
					ParamInfo c = p;
					c.name         = in.name + " " + channels[ k ];
					c.displayName  = p.displayName + " " + channels[ k ];
					c.kind         = kinds[ k ];
					c.defaultValue = std::clamp( in.defaults[ k ], 0.0f, 1.0f );
					params.push_back( c );
				}
				continue;
			}
			default: continue; // images and sound: no parameter
			}
			params.push_back( p );
		}
		InitValues();
	}

	std::string Name() const override { return shader->name; }
	std::string Id() const override { return id; }
	bool IsSource() const override { return shader->source; }
	std::string Error() const override { return error; }

	bool Process( GLuint input, int width, int height, GLuint targetFbo, const FrameInfo& frame ) override
	{
		Helpers& helpers = Shared();
		Compiled* c      = CompiledFor( *shader, error );
		if( !c || !helpers.ok )
		{
			if( !helpers.ok )
				error = helpers.straighten.Error() + helpers.premultiply.Error();
			return false;
		}
		glBindVertexArray( helpers.vao );
		glDisable( GL_BLEND );

		// The input in straight colour, as ISF shaders see it.
		if( !shader->source && input )
		{
			straight.Ensure( width, height );
			straight.Bind();
			DrawThrough( helpers.straighten, input );
		}

		// What pass sizes may refer to.
		std::map< std::string, double > variables = { { "WIDTH", width }, { "HEIGHT", height } };
		for( size_t i = 0; i < shader->inputs.size(); ++i )
		{
			const IsfInput& in = shader->inputs[ i ];
			if( in.type == IsfInput::Type::Float || in.type == IsfInput::Type::Long || in.type == IsfInput::Type::Bool )
				variables[ in.name ] = effective[ first[ i ] ];
		}

		std::time_t now = std::time( nullptr );
		std::tm local   = *std::localtime( &now );
		const std::vector< IsfPass >& passes = shader->passes;
		for( size_t pass = 0; pass < passes.size(); ++pass )
		{
			const IsfPass& p = passes[ pass ];
			int w = width, h = height;
			double v = 0;
			if( !p.width.empty() && EvalIsfExpression( p.width, variables, v ) )
				w = std::clamp( (int)std::lround( v ), 1, 8192 );
			if( !p.height.empty() && EvalIsfExpression( p.height, variables, v ) )
				h = std::clamp( (int)std::lround( v ), 1, 8192 );
			Target* target = nullptr;
			if( !p.target.empty() )
			{
				target = &targets[ p.target ];
				if( !target->Ensure( w, h, p.floating ) )
				{
					error = "could not make the buffer " + p.target;
					return false;
				}
				glBindFramebuffer( GL_FRAMEBUFFER, target->framebuffer[ 1 - target->front ] );
				glViewport( 0, 0, w, h );
			}
			else
			{
				w = width;
				h = height;
				glBindFramebuffer( GL_FRAMEBUFFER, targetFbo );
				glViewport( 0, 0, width, height );
			}

			c->program.Use();
			glUniform1i( c->Uniform( "isf_premultiply" ), target ? 0 : 1 );
			glUniform1i( c->Uniform( "PASSINDEX" ), (int)pass );
			glUniform2f( c->Uniform( "RENDERSIZE" ), (float)w, (float)h );
			glUniform1f( c->Uniform( "TIME" ), (float)frame.time );
			glUniform1f( c->Uniform( "TIMEDELTA" ), (float)frame.delta );
			glUniform4f( c->Uniform( "DATE" ), (float)( local.tm_year + 1900 ), (float)( local.tm_mon + 1 ), (float)local.tm_mday,
			             (float)( local.tm_hour * 3600 + local.tm_min * 60 + local.tm_sec ) );
			glUniform1i( c->Uniform( "FRAMEINDEX" ), frameIndex );

			int unit = 0;
			auto image = [ & ]( const std::string& name, GLuint texture, int tw, int th ) {
				glActiveTexture( GL_TEXTURE0 + unit );
				glBindTexture( GL_TEXTURE_2D, texture );
				glUniform1i( c->Uniform( name ), unit );
				glUniform2f( c->Uniform( "_" + name + "_imgSize" ), (float)tw, (float)th );
				glUniform4f( c->Uniform( "_" + name + "_imgRect" ), 0, 0, (float)tw, (float)th );
				glUniform1i( c->Uniform( "_" + name + "_flip" ), 0 );
				++unit;
			};
			for( size_t i = 0; i < shader->inputs.size(); ++i )
			{
				const IsfInput& in = shader->inputs[ i ];
				const float* e     = effective.data() + ( first[ i ] < (int)effective.size() ? first[ i ] : 0 );
				switch( in.type )
				{
				case IsfInput::Type::Event:
				case IsfInput::Type::Bool: glUniform1i( c->Uniform( in.name ), e[ 0 ] >= 0.5f ? 1 : 0 ); break;
				case IsfInput::Type::Long: glUniform1i( c->Uniform( in.name ), (int)std::lround( e[ 0 ] ) ); break;
				case IsfInput::Type::Float: glUniform1f( c->Uniform( in.name ), e[ 0 ] ); break;
				case IsfInput::Type::Point2D: glUniform2f( c->Uniform( in.name ), e[ 0 ], e[ 1 ] ); break;
				case IsfInput::Type::Color: glUniform4f( c->Uniform( in.name ), e[ 0 ], e[ 1 ], e[ 2 ], e[ 3 ] ); break;
				default:
					// The input image; other images and sound are not fed (black).
					if( in.name == "inputImage" && !shader->source && input )
						image( in.name, straight.Texture(), width, height );
					else
						image( in.name, 0, 1, 1 );
					break;
				}
			}
			for( auto& [ name, t ] : targets )
				image( name, t.texture[ t.front ], t.width, t.height );
			glDrawArrays( GL_TRIANGLE_STRIP, 0, 4 );
			if( target )
				target->front = 1 - target->front;
		}

		// A last pass into a buffer: the buffer is the picture.
		if( !passes.back().target.empty() )
		{
			Target& t = targets[ passes.back().target ];
			glBindFramebuffer( GL_FRAMEBUFFER, targetFbo );
			glViewport( 0, 0, width, height );
			DrawThrough( helpers.premultiply, t.texture[ t.front ] );
		}
		glBindFramebuffer( GL_FRAMEBUFFER, targetFbo );
		glViewport( 0, 0, width, height );
		++frameIndex;
		ResetGlState();
		return true;
	}

private:
	std::shared_ptr< const IsfShader > shader;
	std::string id, error;
	std::vector< int > first; // each input's first parameter
	Fbo straight;
	std::map< std::string, Target > targets;
	int frameIndex = 0;
};
}

std::unique_ptr< Effect > CreateIsfEffect( std::shared_ptr< const IsfShader > shader, const std::string& id )
{
	if( !shader )
		return nullptr;
	return std::make_unique< IsfEffect >( std::move( shader ), id );
}

bool CheckIsfShader( std::shared_ptr< const IsfShader > shader, std::string& error )
{
	// A small picture to run it on: grey with a white square, opaque.
	Fbo in, out;
	if( !in.Ensure( 64, 64 ) || !out.Ensure( 64, 64 ) )
	{
		error = "no render target";
		return false;
	}
	in.Bind();
	glClearColor( 0.5f, 0.5f, 0.5f, 1 );
	glClear( GL_COLOR_BUFFER_BIT );
	glEnable( GL_SCISSOR_TEST );
	glScissor( 16, 16, 32, 32 );
	glClearColor( 1, 1, 1, 1 );
	glClear( GL_COLOR_BUFFER_BIT );
	glDisable( GL_SCISSOR_TEST );
	std::unique_ptr< Effect > effect = CreateIsfEffect( shader, "isf:" + shader->name );
	FrameInfo frame;
	bool ok = true;
	while( glGetError() != GL_NO_ERROR )
	{
	}
	for( int i = 0; i < 3 && ok; ++i ) // a few frames, so feedback buffers are read back too
	{
		frame.time += 1.0 / 60;
		frame.delta = 1.0 / 60;
		effect->Update( frame );
		out.Bind();
		glClearColor( 0, 0, 0, 0 );
		glClear( GL_COLOR_BUFFER_BIT );
		ok = effect->Process( shader->source ? 0 : in.Texture(), 64, 64, out.Framebuffer(), frame );
	}
	GLenum glError = glGetError();
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
	if( !ok )
	{
		error = effect->Error();
		return false;
	}
	if( glError != GL_NO_ERROR )
	{
		error = "OpenGL error " + std::to_string( glError );
		return false;
	}
	return true;
}

void ReleaseIsfPrograms()
{
	Programs().clear();
	std::unique_ptr< Helpers >& helpers = SharedSlot();
	if( helpers && helpers->vao )
		glDeleteVertexArrays( 1, &helpers->vao );
	helpers.reset();
}
}
