#pragma once
#include <memory>
#include <string>
#include <vector>
#include "fx/Effect.h"

namespace stltvj
{
struct BuiltinParam
{
	const char* name;
	ParamKind kind;
	float min, max, defaultValue;
	std::vector< const char* > options; // Option kind: element names, values 0, 1, 2...
};

// A GLSL effect or source shipped with the app.
struct BuiltinDef
{
	const char* name;
	bool source;
	bool feedback; // reads its own previous output (uPrevious)
	std::vector< BuiltinParam > params;
	// Defines `vec4 effect( vec2 uv )` returning premultiplied RGBA. Available: src( uv ), uPrevious, uResolution,
	// uTime, uBeat, parameters p0..p11, and the helpers unpremultiply, rgb2hsv, hsv2rgb, luma.
	const char* glsl;
};

const std::vector< BuiltinDef >& BuiltinDefs();
std::unique_ptr< Effect > CreateBuiltin( const std::string& name );
// Frees the shared GLSL programs (main context, at shutdown).
void ReleaseBuiltinPrograms();
}
