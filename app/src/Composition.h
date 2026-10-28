#pragma once
#include <memory>
#include <string>
#include <vector>
#include "fx/Effect.h"
#include "render/ClipPlayer.h"
#include "render/Fbo.h"

namespace stltvj
{
class AudioVoice;

enum class BlendMode
{
	Alpha,
	Add,
	Screen,
	Multiply,
	Lighten,
	Darken,
	Difference,
	Subtract,
	LumaKey,         // the layer shows where it is bright
	LumaKeyInverted, // the layer shows where it is dark
	Count
};
const char* BlendModeName( BlendMode mode );

enum class PlayMode
{
	Loop,
	Bounce,
	Once,      // and holds the last frame
	OnceClear, // and clears the layer
	Count
};
const char* PlayModeName( PlayMode mode );

// How a playing clip gives way to the next one launched on its layer.
enum class TransitionMode
{
	Fade,      // crossfade
	FadeBlack, // out to black, then in
	WipeLeft,  // the new clip comes in from the right
	WipeRight,
	WipeUp,
	WipeDown,
	Count
};
const char* TransitionModeName( TransitionMode mode );

// Where a launched clip plays: on the layer of its own row, or on the selected layer whatever its row
// (Resolume's "layer based" deck).
enum class ClipTarget
{
	OwnLayer,
	SelectedLayer,
	Count
};
const char* ClipTargetName( ClipTarget target );

// When a launched clip starts: at once, or on the next beat or bar (4 beats).
enum class LaunchQuantize
{
	Now,
	Beat,
	Bar,
	Count
};
const char* LaunchQuantizeName( LaunchQuantize quantize );

// The clip settings that can follow the beat.
enum class ClipProperty
{
	Opacity,
	PositionX,
	PositionY,
	Scale,
	Rotation,
	Count
};
const char* ClipPropertyName( ClipProperty property );

constexpr int kCuePoints = 8;

// How a clip fills the composition before its own transform.
enum class FitMode
{
	Fit,     // whole clip visible, aspect kept
	Fill,    // composition covered, aspect kept
	Stretch, // composition covered exactly
	Native,  // one clip pixel per composition pixel
	Count
};
const char* FitModeName( FitMode mode );

struct Clip
{
	std::string path;
	std::string name;
	float speed       = 1.0f;
	int direction     = 1; // +1, -1, or 0 for paused
	PlayMode playMode = PlayMode::Loop;
	float opacity     = 1.0f;
	FitMode fit       = FitMode::Fit;
	float positionX   = 0.0f; // composition pixels from centre
	float positionY   = 0.0f;
	float scale       = 1.0f;
	float rotation    = 0.0f; // degrees
	float volume      = 1.0f; // the clip's sound, 0..1
	// The part of the movie that plays (seconds; out < 0: to the end), and up to 8 cue points to jump to
	// (seconds; < 0: not set).
	double inPoint = 0, outPoint = -1;
	double cues[ kCuePoints ] = { -1, -1, -1, -1, -1, -1, -1, -1 };
	// BPM sync: the range lasts this many beats, whatever the tempo (0: free, played at `speed`).
	float syncBeats = 0;
	// Beat-synced movement of opacity, position, scale and rotation (from/to: 0..1 of PropertyRange).
	Animation animations[ (int)ClipProperty::Count ];

	EffectChain effects;

	// A generated clip (built-in or FFGL source) instead of a movie: `path` is empty, the source renders
	// at the composition size. generatorState holds its settings until the registry recreates it.
	std::string generatorId;
	std::string generatorState;
	std::unique_ptr< Effect > generator;

	// Runtime state.
	std::unique_ptr< ClipPlayer > player;
	std::unique_ptr< Fbo > generatorThumbnail;
	std::shared_ptr< AudioVoice > audio; // the clip's sound, when it has one
	double time        = 0.0; // seconds into the movie
	int bounceSign     = 1;
	bool finished      = false;

	bool Loaded() const { return player && player->Error().empty() && player->FrameCount() > 0; }
	bool IsGenerator() const { return !generatorId.empty(); }
	bool Empty() const { return path.empty() && generatorId.empty(); }
	// Something that can play: a loaded movie or a live generator.
	bool Playable() const { return Loaded() || generator != nullptr; }
	double Duration() const { return Loaded() ? player->Source().Duration() : 0.0; }
	// The playing range, from the in and out points (never empty).
	double RangeStart() const;
	double RangeEnd() const;
	// Movie seconds per composition second: `speed`, or what fits the range into syncBeats beats.
	double Rate( double bpm ) const;
	// Moves the playhead by `seconds` of composition time, within the range.
	void Advance( double seconds, double bpm );
	void Restart();
	// Puts the playhead at `seconds` (kept within the range), playing on from there.
	void JumpTo( double seconds );
	int64_t CurrentFrame() const;
	int PlayDirection() const;
	// A property as shown now: its value, or where its beat animation is.
	float Shown( ClipProperty property, double beat, float compWidth, float compHeight ) const;
	float Value( ClipProperty property ) const;
	static void PropertyRange( ClipProperty property, float compWidth, float compHeight, float& min, float& max );
};

struct Layer
{
	std::string name;
	std::vector< Clip > clips; // one per column; empty paths are empty cells
	int active        = -1;    // playing column, or -1
	int activeRow     = -1;    // the row (layer index) that column is in: this layer's own, or another one
	float master      = 1.0f; // video and sound together
	float opacity     = 1.0f;
	float volume      = 1.0f; // the sound of its clips
	float speed       = 1.0f; // times the clips' own speed
	int crossfader    = 0;    // crossfader group: 0 none, 1 A, 2 B
	BlendMode blend   = BlendMode::Alpha;
	bool bypass       = false;
	bool solo         = false;
	EffectChain effects;
	Animation opacityAnimation; // beat-synced opacity (0..1)
	// Launching a clip while another plays: a transition of this length (seconds, or beats with
	// transitionInBeats; 0: cut).
	TransitionMode transition = TransitionMode::Fade;
	float transitionDuration  = 0;
	bool transitionInBeats    = false;

	// Runtime: the clip fading out during a transition (column and row; -1: none) and how far the transition is
	// (0..1).
	int previous             = -1;
	int previousRow          = -1;
	float transitionProgress = 1;

	float ShownOpacity( double beat ) const;
	// How much the crossfader lets through (1 outside the groups): each side fades out over the far half.
	float CrossfadeGain( float crossfader ) const;
	double TransitionSeconds( double bpm ) const;
};

struct Composition
{
	std::string name = "Untitled";
	int width        = 1920;
	int height       = 1080;
	float master     = 1.0f;
	float audioVolume = 1.0f; // master volume of every clip's sound
	float speed      = 1.0f;
	double bpm       = 120.0;
	bool paused      = false;
	bool bypass      = false; // the whole output black
	float crossfader = 0.5f;  // 0: group A only, 1: group B only
	double beat      = 0.0; // beats since start (runtime): drives beat-synced effects and animations
	LaunchQuantize quantize = LaunchQuantize::Now;
	ClipTarget target       = ClipTarget::OwnLayer;
	std::vector< std::string > columns;
	std::vector< Layer > layers; // index 0 is the bottom layer
	EffectChain effects;

	int selectedLayer  = 0;  // the layer the Layer panel shows (and layer based launches go to)
	int selectedRow    = 0;  // the selected clip: its row and column
	int selectedColumn = -1;
	int activeColumn   = -1;

	static Composition MakeDefault();
	int ColumnCount() const { return (int)columns.size(); }
	void AddLayer();
	void AddColumn();
	Clip* SelectedClip();
	Clip* ClipAt( int row, int column );
	// The clip a layer plays, and the one it fades out during a transition (null: none).
	Clip* ActiveClip( int layer );
	Clip* OutgoingClip( int layer );
	// The layer playing the clip at row/column, or fading it out; -1 when it is not on show.
	int LayerShowing( int row, int column ) const;
	// Makes every layer as wide as the column list.
	void Normalise();
};

// Saving and loading as JSON. Clips are stored by path; players are created by the caller.
std::string SaveComposition( const Composition& composition );
bool LoadComposition( const std::string& json, Composition& composition, std::string& error );
}
