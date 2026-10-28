#pragma once
#include <memory>
#include <string>
#include <vector>
#include "render/ClipPlayer.h"

namespace stltvj
{
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
	Count
};
const char* BlendModeName( BlendMode mode );

enum class PlayMode
{
	Loop,
	Bounce,
	Once,
	Count
};
const char* PlayModeName( PlayMode mode );

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

	// Runtime state.
	std::unique_ptr< ClipPlayer > player;
	double time        = 0.0; // seconds into the movie
	int bounceSign     = 1;
	bool finished      = false;

	bool Loaded() const { return player && player->Error().empty() && player->FrameCount() > 0; }
	double Duration() const { return Loaded() ? player->Video().DurationSeconds() : 0.0; }
	// Moves the playhead by `seconds` of composition time.
	void Advance( double seconds );
	void Restart();
	int64_t CurrentFrame() const;
	int PlayDirection() const;
};

struct Layer
{
	std::string name;
	std::vector< Clip > clips; // one per column; empty paths are empty cells
	int active        = -1;    // playing column, or -1
	float opacity     = 1.0f;
	BlendMode blend   = BlendMode::Alpha;
	bool bypass       = false;
	bool solo         = false;
};

struct Composition
{
	std::string name = "Untitled";
	int width        = 1920;
	int height       = 1080;
	float master     = 1.0f;
	float speed      = 1.0f;
	double bpm       = 120.0;
	bool paused      = false;
	std::vector< std::string > columns;
	std::vector< Layer > layers; // index 0 is the bottom layer

	int selectedLayer  = 0;
	int selectedColumn = -1;
	int activeColumn   = -1;

	static Composition MakeDefault();
	int ColumnCount() const { return (int)columns.size(); }
	void AddLayer();
	void AddColumn();
	Clip* SelectedClip();
	// Makes every layer as wide as the column list.
	void Normalise();
};

// Saving and loading as JSON. Clips are stored by path; players are created by the caller.
std::string SaveComposition( const Composition& composition );
bool LoadComposition( const std::string& json, Composition& composition, std::string& error );
}
