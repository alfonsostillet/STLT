#include "fx/Isf.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <json.hpp>
#include <regex>
#include <set>

namespace stltvj
{
namespace
{
using nlohmann::json;

float Number( const json& j, float fallback )
{
	if( j.is_number() )
		return j.get< float >();
	if( j.is_boolean() )
		return j.get< bool >() ? 1.0f : 0.0f;
	return fallback;
}

// ISF headers are hand-written: trailing commas happen.
std::string WithoutTrailingCommas( const std::string& text )
{
	return std::regex_replace( text, std::regex( R"(,(\s*[\]\}]))" ), "$1" );
}

// A name from the code as a label: "gridSize" or "grid_size" as "Grid Size".
std::string Label( const std::string& name )
{
	std::string out;
	for( size_t i = 0; i < name.size(); ++i )
	{
		char c = name[ i ];
		if( c == '_' )
			c = ' ';
		else if( i > 0 && std::isupper( (unsigned char)c ) && std::islower( (unsigned char)name[ i - 1 ] ) )
			out += ' ';
		out += c;
	}
	for( size_t i = 0; i < out.size(); ++i )
		if( i == 0 || out[ i - 1 ] == ' ' )
			out[ i ] = (char)std::toupper( (unsigned char)out[ i ] );
	return out;
}

std::string Replace( const std::string& text, const char* pattern, const char* with )
{
	return std::regex_replace( text, std::regex( pattern ), with );
}

// What both stages read: ISF's built-in uniforms, the inputs and pass targets, and the IMG_ functions (the
// images here are whole textures, so the functions need no per-image rectangles).
std::string Declarations( const IsfShader& s )
{
	std::string d = R"(
#define IMG_NORM_PIXEL( i, c ) texture( i, c )
#define IMG_PIXEL( i, c ) texture( i, ( c ) / vec2( textureSize( i, 0 ) ) )
#define IMG_SIZE( i ) vec2( textureSize( i, 0 ) )
#define IMG_THIS_PIXEL( i ) texture( i, isf_FragNormCoord )
#define IMG_THIS_NORM_PIXEL( i ) texture( i, isf_FragNormCoord )
uniform int PASSINDEX;
uniform vec2 RENDERSIZE;
uniform float TIME;
uniform float TIMEDELTA;
uniform vec4 DATE;
uniform int FRAMEINDEX;
)";
	auto image = [ & ]( const std::string& name ) {
		d += "uniform sampler2D " + name + ";\nuniform vec2 _" + name + "_imgSize;\nuniform vec4 _" + name + "_imgRect;\nuniform bool _" + name + "_flip;\n";
	};
	std::set< std::string > images;
	for( const IsfInput& in : s.inputs )
		switch( in.type )
		{
		case IsfInput::Type::Event:
		case IsfInput::Type::Bool: d += "uniform bool " + in.name + ";\n"; break;
		case IsfInput::Type::Long: d += "uniform int " + in.name + ";\n"; break;
		case IsfInput::Type::Float: d += "uniform float " + in.name + ";\n"; break;
		case IsfInput::Type::Point2D: d += "uniform vec2 " + in.name + ";\n"; break;
		case IsfInput::Type::Color: d += "uniform vec4 " + in.name + ";\n"; break;
		default:
			if( images.insert( in.name ).second )
				image( in.name );
			break;
		}
	for( const IsfPass& p : s.passes )
		if( !p.target.empty() && images.insert( p.target ).second )
			image( p.target );
	return d;
}

// GLSL 1.x idioms ISF shaders are written in, in their 4.10 core form.
std::string Modernise( std::string text, bool vertex )
{
	text = Replace( text, R"(#version[^\n]*)", "" );
	text = Replace( text, R"(#extension[^\n]*)", "" ); // what they enable is core in 4.10, and they must come first
	text = Replace( text, R"(\bgl_FragColor\b)", "isf_FragColor" );
	text = Replace( text, R"(\bgl_FragData\s*\[\s*0\s*\])", "isf_FragColor" );
	text = Replace( text, R"(\btexture2DRect\b)", "texture" );
	text = Replace( text, R"(\btexture2D\b)", "texture" );
	text = Replace( text, R"(\bsampler2DRect\b)", "sampler2D" );
	text = Replace( text, R"(\bvv_FragNormCoord\b)", "isf_FragNormCoord" );
	text = Replace( text, R"(\bvv_vertShaderInit\b)", "isf_vertShaderInit" );
	text = Replace( text, R"(\bvarying\b)", vertex ? "out" : "in" );
	return text;
}
}

bool ParseIsf( const std::string& name, const std::string& fs, const std::string& vs, IsfShader& shader, std::string& error )
{
	size_t open = fs.find( "/*" ), close = open == std::string::npos ? open : fs.find( "*/", open + 2 );
	if( open == std::string::npos || close == std::string::npos )
	{
		error = "no ISF header (the JSON comment at the top)";
		return false;
	}
	json header = json::parse( WithoutTrailingCommas( fs.substr( open + 2, close - open - 2 ) ), nullptr, false );
	if( header.is_discarded() || !header.is_object() )
	{
		error = "the ISF header is not valid JSON";
		return false;
	}
	IsfShader s;
	s.name        = name;
	s.description = header.value( "DESCRIPTION", "" );
	s.credit      = header.value( "CREDIT", "" );
	if( header.contains( "CATEGORIES" ) && header[ "CATEGORIES" ].is_array() )
		for( const json& c : header[ "CATEGORIES" ] )
			if( c.is_string() )
				s.categories.push_back( c.get< std::string >() );

	bool hasInputImage = false;
	if( header.contains( "INPUTS" ) && header[ "INPUTS" ].is_array() )
		for( const json& j : header[ "INPUTS" ] )
		{
			if( !j.is_object() || !j.contains( "NAME" ) || !j[ "NAME" ].is_string() )
				continue;
			IsfInput in;
			in.name         = j[ "NAME" ].get< std::string >();
			in.label        = j.contains( "LABEL" ) && j[ "LABEL" ].is_string() && !j[ "LABEL" ].get< std::string >().empty() ? j[ "LABEL" ].get< std::string >() : Label( in.name );
			std::string type = j.value( "TYPE", "float" );
			const json none;
			const json& def = j.contains( "DEFAULT" ) ? j[ "DEFAULT" ] : none;
			const json& mn  = j.contains( "MIN" ) ? j[ "MIN" ] : none;
			const json& mx  = j.contains( "MAX" ) ? j[ "MAX" ] : none;
			if( type == "event" )
				in.type = IsfInput::Type::Event;
			else if( type == "bool" )
			{
				in.type          = IsfInput::Type::Bool;
				in.defaults[ 0 ] = Number( def, 0 );
			}
			else if( type == "long" )
			{
				in.type = IsfInput::Type::Long;
				if( j.contains( "VALUES" ) && j[ "VALUES" ].is_array() )
					for( const json& v : j[ "VALUES" ] )
						in.values.push_back( (int)Number( v, 0 ) );
				if( j.contains( "LABELS" ) && j[ "LABELS" ].is_array() )
					for( const json& l : j[ "LABELS" ] )
						in.labels.push_back( l.is_string() ? l.get< std::string >() : l.dump() );
				in.labels.resize( in.values.size() );
				for( size_t k = 0; k < in.values.size(); ++k )
					if( in.labels[ k ].empty() )
						in.labels[ k ] = std::to_string( in.values[ k ] );
				in.hasRange      = mn.is_number() && mx.is_number();
				in.min           = Number( mn, in.values.empty() ? 0.0f : (float)*std::min_element( in.values.begin(), in.values.end() ) );
				in.max           = Number( mx, in.values.empty() ? 10.0f : (float)*std::max_element( in.values.begin(), in.values.end() ) );
				in.defaults[ 0 ] = Number( def, in.values.empty() ? in.min : (float)in.values[ 0 ] );
			}
			else if( type == "float" )
			{
				in.type          = IsfInput::Type::Float;
				in.hasRange      = mn.is_number() && mx.is_number();
				in.min           = Number( mn, 0 );
				in.max           = Number( mx, std::max( 1.0f, in.min + 1 ) );
				in.defaults[ 0 ] = Number( def, in.min );
			}
			else if( type == "point2D" )
			{
				in.type          = IsfInput::Type::Point2D;
				in.defaults[ 0 ] = def.is_array() && def.size() > 0 ? Number( def[ 0 ], 0 ) : 0;
				in.defaults[ 1 ] = def.is_array() && def.size() > 1 ? Number( def[ 1 ], 0 ) : 0;
				in.hasRange      = mn.is_array() && mx.is_array() && mn.size() >= 2 && mx.size() >= 2;
				if( in.hasRange )
				{
					in.min  = Number( mn[ 0 ], 0 );
					in.minY = Number( mn[ 1 ], 0 );
					in.max  = Number( mx[ 0 ], 1 );
					in.maxY = Number( mx[ 1 ], 1 );
				}
				else
				{
					// No range: pixels when the default looks like pixels, else the unit square.
					float big = std::max( in.defaults[ 0 ], in.defaults[ 1 ] );
					in.min = in.minY = 0;
					in.max = in.maxY = big > 1 ? std::max( 1920.0f, big * 2 ) : 1.0f;
				}
			}
			else if( type == "color" )
			{
				in.type = IsfInput::Type::Color;
				for( int k = 0; k < 4; ++k )
					in.defaults[ k ] = def.is_array() && (int)def.size() > k ? Number( def[ k ], k == 3 ? 1.0f : 0.0f ) : ( k == 3 ? 1.0f : 0.0f );
			}
			else if( type == "image" )
			{
				in.type = IsfInput::Type::Image;
				hasInputImage |= in.name == "inputImage";
			}
			else if( type == "audio" || type == "audioFFT" )
			{
				in.type     = type == "audio" ? IsfInput::Type::Audio : IsfInput::Type::AudioFFT;
				s.usesAudio = true;
			}
			else
				continue; // a type this host does not know: the shader will tell if it needs it
			s.inputs.push_back( in );
		}
	s.source = !hasInputImage;

	// Images to import: declared so the shader compiles, but not loaded.
	if( header.contains( "IMPORTED" ) && ( header[ "IMPORTED" ].is_object() || header[ "IMPORTED" ].is_array() ) )
	{
		s.usesImports = !header[ "IMPORTED" ].empty();
		auto add      = [ & ]( const std::string& n ) {
            IsfInput in;
            in.name = n;
            in.type = IsfInput::Type::Image;
            s.inputs.push_back( in );
		};
		if( header[ "IMPORTED" ].is_object() )
			for( auto it = header[ "IMPORTED" ].begin(); it != header[ "IMPORTED" ].end(); ++it )
				add( it.key() );
		else
			for( const json& j : header[ "IMPORTED" ] )
				if( j.is_object() && j.contains( "NAME" ) && j[ "NAME" ].is_string() )
					add( j[ "NAME" ].get< std::string >() );
	}

	// ISF 1 named its persistent buffers apart, with their sizes.
	std::map< std::string, json > persistent;
	if( header.contains( "PERSISTENT_BUFFERS" ) )
	{
		const json& p = header[ "PERSISTENT_BUFFERS" ];
		if( p.is_array() )
			for( const json& n : p )
				if( n.is_string() )
					persistent[ n.get< std::string >() ] = json::object();
		if( p.is_object() )
			for( auto it = p.begin(); it != p.end(); ++it )
				persistent[ it.key() ] = it.value();
	}
	auto text = [ & ]( const json& j, const char* key ) -> std::string {
		if( !j.is_object() || !j.contains( key ) )
			return "";
		return j[ key ].is_string() ? j[ key ].get< std::string >() : j[ key ].is_number() ? j[ key ].dump() : "";
	};
	if( header.contains( "PASSES" ) && header[ "PASSES" ].is_array() )
		for( const json& j : header[ "PASSES" ] )
		{
			if( !j.is_object() )
				continue;
			IsfPass p;
			p.target     = j.value( "TARGET", "" );
			p.persistent = Number( j.contains( "PERSISTENT" ) ? j[ "PERSISTENT" ] : json(), 0 ) != 0 || persistent.count( p.target );
			p.floating   = Number( j.contains( "FLOAT" ) ? j[ "FLOAT" ] : json(), 0 ) != 0;
			p.width      = text( j, "WIDTH" );
			p.height     = text( j, "HEIGHT" );
			if( persistent.count( p.target ) )
			{
				const json& b = persistent[ p.target ];
				if( p.width.empty() )
					p.width = text( b, "WIDTH" );
				if( p.height.empty() )
					p.height = text( b, "HEIGHT" );
				p.floating |= Number( b.is_object() && b.contains( "FLOAT" ) ? b[ "FLOAT" ] : json(), 0 ) != 0;
			}
			s.passes.push_back( p );
		}
	if( s.passes.empty() )
		s.passes.push_back( IsfPass{} );

	// The shaders, rewritten around ISF's names.
	std::string declarations = Declarations( s );
	std::string body         = fs.substr( 0, open ) + std::string( close + 2 - open, ' ' ) + fs.substr( close + 2 );
	body                     = Modernise( body, false );
	body                     = Replace( body, R"(\bvoid\s+main\s*\()", "void isf_main(" );
	s.fragment = "in vec2 isf_FragNormCoord;\nout vec4 isf_FragColor;\nuniform bool isf_premultiply;\n" + declarations + "#line 1\n" + body +
	             "\nvoid main()\n{\n\tisf_main();\n\tif( isf_premultiply )\n\t\tisf_FragColor = vec4( isf_FragColor.rgb * clamp( isf_FragColor.a, 0.0, 1.0 ), "
	             "clamp( isf_FragColor.a, 0.0, 1.0 ) );\n}\n";
	std::string init = R"(
out vec2 isf_FragNormCoord;
void isf_vertShaderInit()
{
	vec2 corner = vec2( ( gl_VertexID & 1 ) == 0 ? 0.0 : 1.0, ( gl_VertexID & 2 ) == 0 ? 0.0 : 1.0 );
	gl_Position = vec4( corner * 2.0 - 1.0, 0.0, 1.0 );
	isf_FragNormCoord = corner;
}
)";
	s.vertex = init + declarations + ( vs.empty() ? "void main() { isf_vertShaderInit(); }\n" : "#line 1\n" + Modernise( vs, true ) );
	shader   = std::move( s );
	return true;
}

// Expressions -------------------------------------------------------------------------------------------------

namespace
{
struct Expression
{
	const std::string& text;
	const std::map< std::string, double >& variables;
	size_t at = 0;
	bool ok   = true;

	void Space()
	{
		while( at < text.size() && std::isspace( (unsigned char)text[ at ] ) )
			++at;
	}
	bool Eat( char c )
	{
		Space();
		if( at < text.size() && text[ at ] == c )
		{
			++at;
			return true;
		}
		return false;
	}
	std::string Name()
	{
		Space();
		size_t start = at;
		while( at < text.size() && ( std::isalnum( (unsigned char)text[ at ] ) || text[ at ] == '_' ) )
			++at;
		return text.substr( start, at - start );
	}
	double Sum()
	{
		double v = Product();
		for( ;; )
		{
			if( Eat( '+' ) )
				v += Product();
			else if( Eat( '-' ) )
				v -= Product();
			else
				return v;
		}
	}
	double Product()
	{
		double v = Unary();
		for( ;; )
		{
			if( Eat( '*' ) )
				v *= Unary();
			else if( Eat( '/' ) )
			{
				double d = Unary();
				v        = d != 0 ? v / d : 0;
			}
			else
				return v;
		}
	}
	double Unary()
	{
		if( Eat( '-' ) )
			return -Unary();
		if( Eat( '+' ) )
			return Unary();
		return Atom();
	}
	double Atom()
	{
		Space();
		if( Eat( '(' ) )
		{
			double v = Sum();
			ok &= Eat( ')' );
			return v;
		}
		if( Eat( '$' ) )
		{
			auto it = variables.find( Name() );
			if( it == variables.end() )
			{
				ok = false;
				return 0;
			}
			return it->second;
		}
		if( at < text.size() && ( std::isdigit( (unsigned char)text[ at ] ) || text[ at ] == '.' ) )
		{
			size_t used = 0;
			double v    = 0;
			try
			{
				v = std::stod( text.substr( at ), &used );
			}
			catch( ... )
			{
				ok = false;
			}
			at += used;
			return v;
		}
		std::string function = Name();
		if( function.empty() || !Eat( '(' ) )
		{
			ok = false;
			return 0;
		}
		std::vector< double > args{ Sum() };
		while( Eat( ',' ) )
			args.push_back( Sum() );
		ok &= Eat( ')' );
		double a = args[ 0 ], b = args.size() > 1 ? args[ 1 ] : 0;
		if( function == "floor" )
			return std::floor( a );
		if( function == "ceil" )
			return std::ceil( a );
		if( function == "round" )
			return std::round( a );
		if( function == "abs" )
			return std::fabs( a );
		if( function == "sqrt" )
			return std::sqrt( std::max( a, 0.0 ) );
		if( function == "min" && args.size() > 1 )
			return std::min( a, b );
		if( function == "max" && args.size() > 1 )
			return std::max( a, b );
		if( function == "pow" && args.size() > 1 )
			return std::pow( a, b );
		ok = false;
		return 0;
	}
};
}

bool EvalIsfExpression( const std::string& expression, const std::map< std::string, double >& variables, double& result )
{
	Expression e{ expression, variables };
	double v = e.Sum();
	e.Space();
	if( !e.ok || e.at != expression.size() || !std::isfinite( v ) )
		return false;
	result = v;
	return true;
}
}
