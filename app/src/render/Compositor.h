#pragma once
#include <string>
#include "Composition.h"
#include "gl/Program.h"
#include "render/Fbo.h"

namespace stltvj
{
// Mixes the layers of a composition on the GPU: each layer's clip is drawn into a layer target,
// then blended over the composition so far (ping-pong targets, so every blend mode can read what is below it).
// All colour is premultiplied by alpha; the composition background is opaque black.
class Compositor
{
public:
	bool Init( std::string& error );
	void Shutdown();

	// Main context. Renders the whole composition; the result is CompositionTexture().
	void Render( Composition& composition );
	GLuint CompositionTexture() const { return final.Texture(); }

	// Main context. Draws one clip on black, fitted to the target (previews and grid thumbnails).
	void RenderClipAlone( ClipPlayer& player, Fbo& target );
	// Main context. Decodes the first frame of an inactive player into its thumbnail.
	void MakeThumbnail( ClipPlayer& player );

	// Any context with a vertex array bound: draws an FBO texture fitted into a dstWidth x dstHeight framebuffer.
	void DrawFitted( GLuint texture, int srcWidth, int srcHeight, int dstWidth, int dstHeight, float opacity );

private:
	void DrawClip( ClipPlayer& player, const float transform[ 9 ], float opacity );

	Program clipProgram;
	Program blendProgram;
	Program copyProgram;
	GLuint vao = 0;
	Fbo layerTarget;
	Fbo mix[ 2 ];
	Fbo final;
};
}
