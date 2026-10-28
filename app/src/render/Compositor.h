#pragma once
#include <map>
#include <memory>
#include <string>
#include <utility>
#include "Composition.h"
#include "fx/Effect.h"
#include "gl/Program.h"
#include "render/Fbo.h"

namespace stltvj
{
// Mixes the layers of a composition on the GPU.
//
// Per layer: the clip is drawn into a layer target (a movie straight from its HAP textures; or, when the clip has
// effects or is a generator, first at its own size through its effect chain), the layer's effect chain runs on
// the layer target, and the result is blended over the composition so far (ping-pong targets, so every blend mode
// can read what is below it). The composition's own chain runs last, then the master fader.
// All colour is premultiplied by alpha; the composition background is opaque black.
class Compositor
{
public:
	bool Init( std::string& error );
	void Shutdown();

	// Main context. Renders the whole composition; the result is CompositionTexture().
	void Render( Composition& composition, const FrameInfo& frame );
	GLuint CompositionTexture() const { return final.Texture(); }

	// Main context. Draws one clip with its effects on black, fitted to the target (preview monitor).
	void RenderClipAlone( Clip& clip, Fbo& target, int compWidth, int compHeight, const FrameInfo& frame );
	// Main context. Decodes the first frame of an inactive player into its thumbnail.
	void MakeThumbnail( ClipPlayer& player );
	// Main context. Renders a generator clip once into its thumbnail.
	void MakeGeneratorThumbnail( Clip& clip, const FrameInfo& frame );

	// Any context with a vertex array bound: draws an FBO texture fitted into a dstWidth x dstHeight framebuffer.
	void DrawFitted( GLuint texture, int srcWidth, int srcHeight, int dstWidth, int dstHeight, float opacity );

	// Runs a chain on `source`; returns the target holding the result (`source` or `scratch`).
	Fbo* ApplyChain( EffectChain& chain, Fbo* source, Fbo* scratch, const FrameInfo& frame );

private:
	struct Pair
	{
		Fbo a, b;
	};
	Pair& Targets( int width, int height );
	// The clip as an FBO texture (first row at the bottom) when it needs its own pass; null for a plain movie.
	Fbo* RenderClipSource( Clip& clip, int compWidth, int compHeight, const FrameInfo& frame );
	void DrawClip( ClipPlayer& player, const float transform[ 9 ], float opacity );
	void DrawTexture( GLuint texture, const float transform[ 9 ], float opacity );
	void DrawClipInto( Clip& clip, Fbo& target, const FrameInfo& frame, int compWidth, int compHeight, bool forceFit );

	Program clipProgram;
	Program blendProgram;
	Program copyProgram;
	Program transitionProgram;
	GLuint vao = 0;
	Fbo layerTarget;
	Fbo layerScratch;
	Fbo transitionFrom, transitionTo; // the two clips of a layer in transition
	Fbo mix[ 2 ];
	Fbo final;
	std::map< std::pair< int, int >, std::unique_ptr< Pair > > pool;
};

bool HasActiveEffects( const EffectChain& chain );
}
