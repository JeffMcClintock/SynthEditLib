#pragma once

#include <atomic>
#include <memory>
#include <semaphore>
#include <string>
#include <thread>

#include "WavetableCache.h"

// A single request to load + bake a wavetable off the audio thread. The processor and the
// worker thread both hold a shared_ptr to one of these. The worker only ever touches this
// block - never the processor - which is what makes it safe for the processor to be destroyed
// while a load is still running: the block outlives whichever of the two drops it last.
struct WavetableLoadRequest
{
	// Inputs - immutable once enqueued.
	std::string uri;
	float       sampleRate = 0.0f;

	// Cooperative cancellation. Set by the processor when a newer filename supersedes this
	// request; the worker then skips the expensive load+bake and simply drops the request
	// (freeing it - and any result it already holds - on the worker thread).
	std::atomic<bool> cancelled{false};

	// Output. `result` is fully written before `ready` is set with release ordering; the
	// reader loads `ready` with acquire ordering before it touches `result`.
	std::shared_ptr<CachedWavetable> result;
	std::atomic<bool> ready{false};

	// Disposal-only requests: no load is performed, the worker just drops this reference so
	// the pointee (e.g. a superseded multi-MB bake) is freed off the audio thread.
	std::shared_ptr<void> garbage;

	// Intrusive queue linkage, owned by WavetableLoader. `self` holds the queue's reference
	// while the request sits in the stack, letting the push side stay lock-free. A request
	// may be enqueued only once.
	WavetableLoadRequest* next{};
	std::shared_ptr<WavetableLoadRequest> self;
};

// Background loader: one worker thread draining a lock-free FIFO of requests. Shared between
// every WavetableOsc instance via wavetableLoader(); the worker is joined when the last
// referencing instance releases its shared_ptr - during normal plugin teardown, NOT during
// static destruction. (A static-lifetime loader would join its thread inside the DLL's
// DLL_PROCESS_DETACH, where the Windows loader lock deadlocks against the exiting thread's
// DLL_THREAD_DETACH.)
//
// enqueue() is lock-free (Treiber-stack push + semaphore release) so the audio thread never
// shares a mutex with the normal-priority worker (no priority inversion).
class WavetableLoader
{
public:
	WavetableLoader();
	~WavetableLoader();

	void enqueue(std::shared_ptr<WavetableLoadRequest> request);

	// Free `garbage` on the worker thread. Used by the audio thread to shed possibly-last
	// references to multi-MB objects (superseded requests, replaced bakes) without paying
	// for the deallocation inside the callback. Allocates a small tombstone request -
	// acceptable on the rare file-change control path, unlike the tens-of-MB free it avoids.
	void dispose(std::shared_ptr<void> garbage);

private:
	void run();

	std::atomic<WavetableLoadRequest*> queueHead_{nullptr}; // LIFO; worker reverses to FIFO.
	std::counting_semaphore<>          workAvailable_{0};
	std::atomic<bool>                  stop_{false};
	std::thread                        worker_;
};

// The process-wide shared loader. Each WavetableOsc acquires this once in open() (off the
// audio thread - the first call constructs the loader and spawns its worker) and holds the
// shared_ptr for its lifetime.
std::shared_ptr<WavetableLoader> wavetableLoader();
