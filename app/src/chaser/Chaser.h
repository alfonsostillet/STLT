#pragma once
#include <map>
#include <string>
#include <vector>
#include "output/AdvancedOutput.h"

namespace stltvj
{
// STLT-CHASER inside STLT-VJ: step sequences over the Advanced Output's slices, played to the beat. A step lights
// some slices; the slices it does not light are dark. What was the FFGL plugin in Resolume works here on the
// output itself: each slice's brightness is multiplied by what the chase gives it.

enum class ChaserDriver
{
	Chaser, // the sequence's steps, in order
	Random, // a few slices at random every step
	Count
};
const char* ChaserDriverName( ChaserDriver driver );

struct ChaserSequence
{
	std::string name = "Sequence";
	std::vector< std::vector< std::string > > steps; // the slice ids lit in each step; its size is the length
	bool Lit( int step, const std::string& slice ) const;
	void Toggle( int step, const std::string& slice );
	void Set( int step, const std::string& slice, bool on );
	// Resizes to `count` steps (1..1024): new steps are empty, steps past the end go.
	void Resize( int count );
	// The steps with something in them, repeated after each other over the whole length (->) or back and forth
	// (<->); Clear empties every step.
	void Repeat( bool pingPong );
	void Clear();
};

// The Factory: a sequence from where the slices are, as STLT-CHASER's editor made them.
enum class FactoryOrder
{
	LeftToRight,
	RightToLeft,
	TopToBottom,
	BottomToTop,
	CentreOut,
	OutsideIn,
	Random,
	Count
};
const char* FactoryOrderName( FactoryOrder order );

struct FactorySettings
{
	FactoryOrder order = FactoryOrder::LeftToRight;
	float tolerance    = 0.02f; // slices whose positions differ by less (share of the composition) light together
	int block          = 1;     // groups lit per step
	bool mirror        = false; // from both sides at once (left-right, top-bottom)
	bool fill          = false; // each step keeps the previous ones lit
	bool pingPong      = false; // and back
	unsigned seed      = 1;     // Random
};

// A slice's place for the Factory: its id and the centre of its input (composition pixels).
struct SlicePlace
{
	std::string id;
	float x = 0, y = 0;
};
std::vector< SlicePlace > SlicePlaces( const AdvancedOutputSetup& setup );
// The steps the Factory makes from `places` within a composition of `width` x `height`.
std::vector< std::vector< std::string > > FactorySteps( const std::vector< SlicePlace >& places, float width, float height, const FactorySettings& settings );

struct ChaserSetup
{
	std::vector< ChaserSequence > sequences;
	int active = 0; // the sequence playing (and edited)

	// Playback.
	bool enabled        = false; // on the output (Output > Chaser)
	bool playing        = true;
	ChaserDriver driver = ChaserDriver::Chaser;
	float stepsPerBeat  = 1;   // 0.25: one step a bar (4/4); 4: sixteenths
	float sustain       = 1;   // steps a slice stays full after its step starts
	float release       = 0;   // steps it then takes to fade out
	int echoes          = 0;   // earlier steps still showing, dimmer and dimmer
	int randomCount     = 3;   // Random: slices lit per step
	float dark          = 0;   // what the slices not lit show (0: black, 1: the full picture)

	// Runtime, not saved.
	double position = 0; // in steps, from the start of the sequence

	ChaserSequence* Active();
	const ChaserSequence* Active() const;
	int Length() const;
	// Moves on with the composition's beat (locked to it while playing).
	void Follow( double beat );
	// The brightness (0..1) of every slice in `ids` at the current position.
	std::map< std::string, float > Gains( const std::vector< std::string >& ids ) const;

	std::string Save() const;
	bool Load( const std::string& json );
};
}
