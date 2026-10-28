#include "ThreadPool.h"
#include <algorithm>
#include <atomic>

namespace stltvj
{
struct ThreadPool::Batch
{
	const std::function< void( unsigned ) >* job = nullptr;
	unsigned count = 0;
	std::atomic< unsigned > next{ 0 };
	unsigned done = 0; // guarded by mutex
	std::mutex mutex;
	std::condition_variable finished;
};

ThreadPool::ThreadPool( unsigned workers )
{
	if( workers == 0 )
		workers = std::max( 1u, std::thread::hardware_concurrency() - 1 );
	for( unsigned i = 0; i < workers; ++i )
		threads.emplace_back( [ this ] { Work(); } );
}

ThreadPool::~ThreadPool()
{
	{
		std::lock_guard< std::mutex > lock( mutex );
		stopping = true;
	}
	wake.notify_all();
	for( std::thread& t : threads )
		t.join();
}

ThreadPool& ThreadPool::Shared()
{
	static ThreadPool pool;
	return pool;
}

// Runs a claimed job. The batch cannot finish, and so cannot be destroyed, before this returns:
// `done` only reaches `count` under the batch mutex, and the caller waits for that under the same mutex.
void ThreadPool::RunJob( Batch& batch, unsigned index )
{
	( *batch.job )( index );
	std::lock_guard< std::mutex > lock( batch.mutex );
	if( ++batch.done == batch.count )
		batch.finished.notify_all();
}

void ThreadPool::Run( unsigned count, const std::function< void( unsigned ) >& job )
{
	if( count == 0 )
		return;
	if( count == 1 )
	{
		job( 0 );
		return;
	}
	Batch batch;
	batch.job   = &job;
	batch.count = count;
	{
		std::lock_guard< std::mutex > lock( mutex );
		batches.push_back( &batch );
	}
	wake.notify_all();

	// The caller works too instead of sleeping.
	for( ;; )
	{
		unsigned index = batch.next.fetch_add( 1 );
		if( index >= count )
			break;
		RunJob( batch, index );
	}
	{
		// Workers claim jobs under this lock, so once the batch is off the list nobody new can touch it.
		std::lock_guard< std::mutex > lock( mutex );
		batches.erase( std::remove( batches.begin(), batches.end(), &batch ), batches.end() );
	}
	std::unique_lock< std::mutex > lock( batch.mutex );
	batch.finished.wait( lock, [ & ] { return batch.done == batch.count; } );
}

void ThreadPool::Work()
{
	for( ;; )
	{
		Batch* batch   = nullptr;
		unsigned index = 0;
		{
			std::unique_lock< std::mutex > lock( mutex );
			wake.wait( lock, [ & ] {
				if( stopping )
					return true;
				for( Batch* b : batches )
				{
					if( b->next.load() >= b->count )
						continue;
					index = b->next.fetch_add( 1 );
					if( index < b->count )
					{
						batch = b;
						return true;
					}
				}
				return false;
			} );
			if( stopping && !batch )
				return;
		}
		RunJob( *batch, index );
	}
}
}
