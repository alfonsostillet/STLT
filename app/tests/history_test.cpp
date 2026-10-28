// Tests for undo and redo by snapshots.
#include <cstdio>
#include "History.h"

using namespace stltvj;

static int failures = 0;

#define CHECK( condition )                                                                           \
	do                                                                                               \
	{                                                                                                \
		if( !( condition ) )                                                                         \
		{                                                                                            \
			std::fprintf( stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #condition ); \
			++failures;                                                                              \
		}                                                                                            \
	} while( 0 )

int main()
{
	History h;
	History::State a{ "comp A", "out A" }, b{ "comp B", "out A" }, c{ "comp B", "out C" }, d{ "comp D", "out C" };
	History::State restore;
	h.Reset( a );
	CHECK( !h.CanUndo() && !h.CanRedo() );
	CHECK( !h.Record( a ) ); // nothing changed
	CHECK( h.Record( b ) && h.UndoLabel() == "Composition" );
	CHECK( h.Record( c ) && h.UndoLabel() == "Advanced Output" );

	// Back twice, forward once.
	CHECK( h.Undo( c, restore ) && restore == b && h.RedoLabel() == "Advanced Output" );
	CHECK( h.Undo( b, restore ) && restore == a && !h.CanUndo() );
	CHECK( !h.Undo( a, restore ) );
	CHECK( h.Redo( a, restore ) && restore == b );

	// An edit made after undoing drops the redo branch, and the edit itself is undoable.
	CHECK( h.CanRedo() );
	CHECK( !h.Redo( d, restore ) ); // d was recorded first
	CHECK( !h.CanRedo() && h.CanUndo() );
	CHECK( h.Undo( d, restore ) && restore == b );

	// Undo records an unrecorded change before stepping back over it.
	h.Reset( a );
	CHECK( h.Undo( b, restore ) && restore == a && h.CanRedo() );
	CHECK( h.Redo( a, restore ) && restore == b );

	// The number of steps is bounded.
	h.Reset( a );
	h.limit = 3;
	for( int i = 0; i < 10; ++i )
		h.Record( { "comp " + std::to_string( i ), "" } );
	int steps = 0;
	History::State current{ "comp 9", "" };
	while( h.Undo( current, restore ) )
	{
		steps++;
		current = restore;
	}
	CHECK( steps == 3 && restore.composition == "comp 6" );

	if( failures )
	{
		std::fprintf( stderr, "%d check(s) failed\n", failures );
		return 1;
	}
	std::printf( "history tests passed\n" );
	return 0;
}
