#pragma once
#include <map>
#include <string>
#include <vector>

namespace stltvj
{
// ISF (Interactive Shader Format, isf.video): a GLSL fragment shader with a JSON header naming its inputs and
// render passes, used by VDMX, MadMapper, Millumin and many free shader collections. This part reads a shader
// and rewrites it for this app's OpenGL 4.1 core pipeline; fx/IsfEffect runs it. No OpenGL here.

struct IsfInput
{
	enum class Type
	{
		Event,
		Bool,
		Long,
		Float,
		Point2D,
		Color,
		Image,
		Audio,
		AudioFFT,
	};
	std::string name, label;
	Type type = Type::Float;
	float min = 0, max = 1;          // Float, Long; Point2D's x and y share them when given as numbers
	float minY = 0, maxY = 1;        // Point2D's y
	float defaults[ 4 ] = {};        // Float/Bool/Long/Event: [0]; Point2D: [0..1]; Color: [0..3]
	bool hasRange = false;
	std::vector< int > values;       // Long: the menu's values
	std::vector< std::string > labels;
};

struct IsfPass
{
	std::string target;              // "" renders to the output
	bool persistent = false;         // the target keeps its content from frame to frame
	bool floating   = false;         // a float target (for feedback that needs precision)
	std::string width, height;       // expressions ("$WIDTH/2.0"); "" is the output's size
};

struct IsfShader
{
	std::string name, description, credit;
	std::vector< std::string > categories;
	std::vector< IsfInput > inputs;
	std::vector< IsfPass > passes;   // at least one
	bool source = false;             // no inputImage: a generator
	bool usesAudio = false;          // audio/audioFFT inputs (not fed yet)
	bool usesImports = false;        // IMPORTED images (not loaded)
	std::string vertex, fragment;    // GLSL 4.10 core, without the #version line
};

// Reads an ISF fragment shader (`fs`) and, when there is one, its vertex shader (`vs`, "" for the default). `name`
// is the file's name without extension. False with `error` when the header cannot be read.
bool ParseIsf( const std::string& name, const std::string& fs, const std::string& vs, IsfShader& shader, std::string& error );

// A pass size expression: numbers, + - * / and brackets, $WIDTH, $HEIGHT and $inputName, and floor, ceil, round,
// abs, min, max, pow, sqrt. False when it cannot be read.
bool EvalIsfExpression( const std::string& expression, const std::map< std::string, double >& variables, double& result );
}
