#pragma once
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
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

	// A picture besides the composition (the Advanced Output's canvases): the timeline's tracks sent to it.
	struct CanvasSize
	{
		std::string id;
		int width = 0, height = 0;
	};
	// Main context. Renders the whole composition; the result is CompositionTexture().
	// `timelineView`: the timeline's video tracks at its playhead instead of the deck's layers; the tracks sent to
	// one of `canvases` are mixed into that canvas (CanvasTexture) instead of the composition. Outside the timeline
	// a canvas shows the composition, stretched to its size.
	void Render( Composition& composition, const FrameInfo& frame, bool timelineView = false, const std::vector< CanvasSize >& canvases = {} );
	GLuint CompositionTexture() const { return final.Texture(); }
	GLuint CompositionFramebuffer() const { return final.Framebuffer(); }
	// A canvas as of the last Render (0: not rendered).
	GLuint CanvasTexture( const std::string& id ) const;
	// One layer alone (the Composition Monitor's Layer button): the index of a deck layer, or of a timeline track in
	// the timeline view (-1: none). Render keeps its picture, effects and all, as it goes into the mix (black when it
	// shows nothing): SoloTexture, of SoloWidth x SoloHeight.
	int soloLayer = -1;
	GLuint SoloTexture() const { return solo.Texture(); }
	int SoloWidth() const { return solo.Width(); }
	int SoloHeight() const { return solo.Height(); }

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
	// The working targets of a mix at one size.
	struct Work
	{
		Fbo layer, scratch, from, to, mix[ 2 ];
	};
	Work& WorkFor( int width, int height );
	// What a mix draws, bottom first: a clip (with an outgoing one in a layer's transition), how it lies, its chain.
	struct Source
	{
		Clip* clip;
		Clip* outgoing;
		float progress;
		TransitionMode transition;
		float opacity;
		BlendMode blend;
		EffectChain* effects;
		bool solo = false; // its picture also kept alone (soloLayer)
	};
	// Mixes `sources` over opaque black at width x height; returns the target holding the result.
	Fbo* Mix( std::vector< Source >& sources, Work& work, int width, int height, const FrameInfo& frame );
	// Copies `from` into `to` with the composition's master and black-out.
	void Finish( Fbo& from, Fbo& to, const Composition& composition );
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
	std::map< std::pair< int, int >, std::unique_ptr< Work > > works;
	Fbo final;
	Fbo solo;
	bool soloDrawn = false;
	void KeepSolo( Fbo& layer, int width, int height );
	std::map< std::string, std::unique_ptr< Fbo > > canvasFinal; // by canvas id
	std::map< std::pair< int, int >, std::unique_ptr< Pair > > pool;
};

bool HasActiveEffects( const EffectChain& chain );
}
