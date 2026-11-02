#pragma once
#include <memory>
#include <string>
#include "fx/Effect.h"
#include "fx/Isf.h"

namespace stltvj
{
// Creates an effect (or a source, for shaders with no inputImage) running an ISF shader: every pass in order,
// persistent and float buffers, the shader's own vertex stage, and its inputs as parameters (a colour as a colour
// picker, a point as X and Y, a long as a menu). `id` is how compositions find it again ("isf:Name").
std::unique_ptr< Effect > CreateIsfEffect( std::shared_ptr< const IsfShader > shader, const std::string& id );

// Builds the shader's programs on the current context and draws one frame of it at 64 x 64 (true when it works),
// for checking a collection.
bool CheckIsfShader( std::shared_ptr< const IsfShader > shader, std::string& error );

// Frees the compiled programs (the context is about to go).
void ReleaseIsfPrograms();
}
