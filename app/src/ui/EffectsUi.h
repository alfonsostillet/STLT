#pragma once
#include <functional>
#include "fx/Effect.h"

namespace stltvj
{
class App;

// Drag-and-drop payload carrying a registry id (an effect or a source from the Effects panel).
constexpr const char* kEffectPayload = "STLT_EFFECT";

// The Effects panel: built-in and FFGL effects and sources to drag onto clips, layers and the composition.
void DrawEffectsPanel( App& app );
// The folders FFGL plugins load from (add, remove, rescan) and the plugins that did not load.
void DrawPluginFolders( App& app );
// An effect chain: one collapsible section per effect with bypass, mix, reorder, remove and its parameters,
// plus a drop zone and an Add menu at the end.
// `scope` is the chain's remote-control address ("/composition", "/composition/layers/2"...): its parameters can
// then be mapped to MIDI.
void DrawEffectChain( App& app, EffectChain& chain, const char* id, const std::string& scope = {} );
// The parameters of one effect (also used for a generator clip's source).
void DrawEffectParams( Effect& effect );
// The beat-animation settings of `anim` (mode, period, from and to over min..max, reverse).
void AnimationControls( Animation& anim, float min, float max );
// A row label of a two-column settings table that opens the beat-animation menu of `anim`; lit while it moves.
void AnimatedLabel( const char* label, Animation& anim, float min, float max );
// Keyframes for effect parameters: while set (the timeline's inspector, for a clip's chain), `button` draws before
// each animatable parameter's name, and `changed` hears of an edit (the value before it, the effect, the parameter).
// `slot` is the place of the effect being drawn in its chain.
struct ParamKeyHooks
{
	std::function< void( Effect&, int slot, int param ) > button;
	std::function< void( Effect&, int slot, int param, float before ) > changed;
	// The same for an effect's Mix (`mix` is its value, to read and set).
	std::function< void( float& mix, int slot ) > mixButton;
	std::function< void( float& mix, int slot ) > mixChanged;
};
extern ParamKeyHooks* paramKeyHooks;
// Accepts an effect dropped on the last item: appends it to `chain`. True if something was dropped.
bool AcceptEffectDrop( App& app, EffectChain& chain );
}
