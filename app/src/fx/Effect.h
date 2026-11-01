#pragma once
#include <memory>
#include <string>
#include <vector>
#include "gl/Gl.h"

namespace stltvj
{
// What a parameter is, as the panels draw it. FFGL parameter types map onto these one to one.
enum class ParamKind
{
	Float,
	Bool,
	Event,   // a button: 1 for one frame
	Option,  // a menu; the value is the chosen element's value
	Integer,
	Text,
	File,
	Red,
	Green,
	Blue,
	Alpha,
	Hue,
	Saturation,
	Brightness,
	XPos,
	YPos,
	Buffer, // audio/FFT data: not fed yet
};

struct ParamInfo
{
	std::string name;        // stable: used to save and restore values
	std::string displayName; // what the panel shows (FFGL 2.3 plugins can change it live)
	ParamKind kind = ParamKind::Float;
	float min = 0, max = 1, defaultValue = 0;
	std::string defaultText;
	std::vector< std::string > options;
	std::vector< float > optionValues;
	std::string group;
	bool visible = true;

	bool IsText() const { return kind == ParamKind::Text || kind == ParamKind::File; }
	bool IsAnimatable() const;
};

// Movement a parameter can have on its own: locked to the composition's beat or timed in seconds, or following
// the sound coming in (AnimationClock's audio levels).
enum class AnimMode
{
	Off,
	Saw,      // min to max, then jump back
	PingPong, // min to max and back
	Sine,
	Square,
	Random,       // a new random value on every period
	SmoothRandom, // from one random value to the next over each period
	AudioLevel,   // the sound's level, 0 (silence) to 1 (full)
	AudioBass,
	AudioMid,
	AudioTreble,
	Count
};
const char* AnimModeName( AnimMode mode );
bool IsAudioMode( AnimMode mode );

struct Animation
{
	AnimMode mode = AnimMode::Off;
	float beats   = 4;   // one period, in beats (or seconds when `seconds`)
	bool seconds  = false;
	float from    = 0;   // 0..1 of the parameter's range
	float to      = 1;
	bool reverse  = false;
	// Position 0..1 within the range at this beat (and the AnimationClock's time and sound).
	float Evaluate( double beat ) const;
};

// What animations follow besides the beat: the time and the sound coming in, set once a frame by the app.
struct AnimationClock
{
	double seconds = 0;
	float audio[ 4 ] = {}; // level, bass, mid, treble, 0..1 (AudioInput's bands)
};
void SetAnimationClock( const AnimationClock& clock );
const AnimationClock& CurrentAnimationClock();
// Whether an animation asked for the sound within the last `seconds` (the app opens the input only then).
bool AudioAnimationWanted( double seconds );

// Timing shared by every effect in a frame.
struct FrameInfo
{
	double time  = 0; // seconds since start
	double delta = 0;
	double bpm   = 120;
	double beat  = 0; // beats since start
};

// An effect or a source (generator) in a chain: built-in GLSL or an FFGL plugin instance.
class Effect
{
public:
	virtual ~Effect() = default;

	virtual std::string Name() const = 0;
	// Registry id ("builtin:Mirror", "ffgl:ABCD:file.dll"): how compositions find the effect again.
	virtual std::string Id() const = 0;
	virtual bool IsSource() const = 0;
	virtual std::string Error() const { return {}; }

	// Renders into `targetFbo`, which is bound with a width x height viewport and cleared to transparent.
	// `input` is 0 for sources. Input and output are premultiplied RGBA with the first row at the bottom.
	virtual bool Process( GLuint input, int width, int height, GLuint targetFbo, const FrameInfo& frame ) = 0;

	// Once per frame before Process: runs animations, pushes changed values, reads plugin-side changes.
	void Update( const FrameInfo& frame );

	const std::vector< ParamInfo >& Params() const { return params; }
	int FindParam( const std::string& name ) const;
	float Value( int index ) const { return values[ index ]; }
	float EffectiveValue( int index ) const { return effective[ index ]; }
	void SetValue( int index, float value );
	const std::string& Text( int index ) const { return texts[ index ]; }
	void SetText( int index, const std::string& text );
	void Trigger( int index ); // event parameters
	Animation& Anim( int index ) { return animations[ index ]; }
	const Animation& Anim( int index ) const { return animations[ index ]; }
	void ResetToDefaults();

protected:
	// Call after `params` is filled.
	void InitValues();
	// Hooks for the backend: a value to pass on, and plugin-side changes to read back.
	virtual void PushValue( int, float ) {}
	virtual void PushText( int, const std::string& ) {}
	virtual void PollChanges() {}
	// Makes the next Update push every value again (after a plugin instance is recreated).
	void ForgetPushed();

	std::vector< ParamInfo > params;
	std::vector< float > values;    // what the user set
	std::vector< float > effective; // after animation: what the effect uses
	std::vector< float > pushed;    // last value handed to the backend
	std::vector< std::string > texts;
	std::vector< Animation > animations;
	std::vector< int > eventFrames; // frames left in an event pulse
};

// One effect in a chain, with its bypass and dry/wet mix.
struct EffectSlot
{
	std::unique_ptr< Effect > effect;
	bool bypass  = false;
	float mix    = 1.0f;
	bool open    = true;
	std::string missingId; // the effect could not be created (plugin not installed): kept so saving does not lose it
	std::string savedState; // JSON of the missing effect's settings
};
using EffectChain = std::vector< EffectSlot >;

// Brings the context to the state FFGL plugins expect: no program, vertex array, buffers or textures bound,
// texture unit 0 active, blending off with the default blend function, depth and culling off.
void ResetGlState();
}
