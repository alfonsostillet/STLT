#pragma once
#include <deque>
#include <string>

namespace stltvj
{
// Undo and redo by snapshots. The app hands over the composition and the output setup as text whenever an edit
// may have ended; a snapshot that differs from the last one becomes an undo step. Restoring is the app's job.
class History
{
public:
	struct State
	{
		std::string composition, output;
		bool operator==( const State& other ) const { return composition == other.composition && output == other.output; }
		bool operator!=( const State& other ) const { return !( *this == other ); }
	};

	// Forgets every step: `current` is the starting point.
	void Reset( State current );
	// Records `current` as a new step if it changed since the last one. True if it did.
	bool Record( State current );
	// Steps back (or forward) from `current`, which is recorded first; `restore` receives the state to apply.
	bool Undo( State current, State& restore );
	bool Redo( State current, State& restore );
	// After restoring, the state as it now reads (which may differ in detail from the one restored): the next step
	// is measured from it.
	void Settle( State current ) { last = std::move( current ); }

	bool CanUndo() const { return !undo.empty(); }
	bool CanRedo() const { return !redo.empty(); }
	// What the next undo or redo changes: "Composition", "Advanced Output" or "" when there is none.
	std::string UndoLabel() const;
	std::string RedoLabel() const;

	size_t limit = 200; // steps kept

private:
	struct Step
	{
		State state;
		std::string label; // what changed between this state and the next one
	};
	static std::string Label( const State& from, const State& to );
	State last;
	bool started = false;
	std::deque< Step > undo, redo;
};
}
