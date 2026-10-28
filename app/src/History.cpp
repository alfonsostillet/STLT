#include "History.h"

namespace stltvj
{
std::string History::Label( const State& from, const State& to )
{
	bool composition = from.composition != to.composition, output = from.output != to.output;
	return composition && output ? "Composition and Output" : output ? "Advanced Output" : "Composition";
}

void History::Reset( State current )
{
	undo.clear();
	redo.clear();
	last    = std::move( current );
	started = true;
}

bool History::Record( State current )
{
	if( !started )
	{
		Reset( std::move( current ) );
		return false;
	}
	if( current == last )
		return false;
	undo.push_back( { std::move( last ), "" } );
	undo.back().label = Label( undo.back().state, current );
	if( undo.size() > limit )
		undo.pop_front();
	redo.clear();
	last = std::move( current );
	return true;
}

bool History::Undo( State current, State& restore )
{
	Record( std::move( current ) );
	if( undo.empty() )
		return false;
	redo.push_back( { std::move( last ), undo.back().label } );
	last = std::move( undo.back().state );
	undo.pop_back();
	restore = last;
	return true;
}

bool History::Redo( State current, State& restore )
{
	// A change made since the last undo starts a new branch: nothing to redo then.
	if( Record( std::move( current ) ) || redo.empty() )
		return false;
	undo.push_back( { std::move( last ), redo.back().label } );
	last = std::move( redo.back().state );
	redo.pop_back();
	restore = last;
	return true;
}

std::string History::UndoLabel() const
{
	return undo.empty() ? std::string() : undo.back().label;
}

std::string History::RedoLabel() const
{
	return redo.empty() ? std::string() : redo.back().label;
}
}
