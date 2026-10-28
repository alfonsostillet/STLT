#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace stltvj
{
// A fixed set of worker threads that run indexed jobs: Run( count, job ) calls job( 0 .. count-1 )
// spread over the workers and the calling thread, and returns when all of them are done.
// Several threads may call Run at the same time; their batches share the workers.
class ThreadPool
{
public:
	explicit ThreadPool( unsigned workers = 0 );
	~ThreadPool();
	ThreadPool( const ThreadPool& ) = delete;
	ThreadPool& operator=( const ThreadPool& ) = delete;

	void Run( unsigned count, const std::function< void( unsigned ) >& job );
	unsigned WorkerCount() const { return (unsigned)threads.size(); }

	// Shared by every player: one pool sized to the machine.
	static ThreadPool& Shared();

private:
	struct Batch;
	void Work();
	static void RunJob( Batch& batch, unsigned index );

	std::vector< std::thread > threads;
	std::mutex mutex;
	std::condition_variable wake;
	std::vector< Batch* > batches;
	bool stopping = false;
};
}
