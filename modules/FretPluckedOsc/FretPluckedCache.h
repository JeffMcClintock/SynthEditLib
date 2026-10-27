#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#if defined(__APPLE__)
#include <dispatch/dispatch.h>
#else
#include <semaphore>
#endif
#include <thread>
#include <vector>

#include "FretPluckedBake.h"

// Same shape as WavetableOsc's cache and loader, keyed on the sound parameters instead of a file.
namespace fret_plucked
{
// Process-wide, weakly held: every voice of a patch shares one bake, freed with its last user.
class TableCache
{
	struct Key
	{
		Params params;
		float sampleRate;
		bool operator==(const Key&) const = default;
	};
	struct Entry
	{
		Key key;
		std::weak_ptr<const Table> table;
	};

	std::mutex mtx_;
	std::condition_variable buildDone_;
	std::vector<Entry> entries_;
	std::vector<Key> building_;

public:
	// The bake runs outside the lock; a second caller for the same key waits for it. Null if cancelled.
	std::shared_ptr<const Table> getOrBake(const Params& params, float sampleRate, const std::atomic<bool>* cancelled = nullptr);
};

TableCache& tableCache();

struct BakeRequest
{
	Params params;
	float sampleRate = 0.0f;

	std::atomic<bool> cancelled{false};

	// `result` is written before `ready` is released.
	std::shared_ptr<const Table> result;
	std::atomic<bool> ready{false};

	// Disposal-only requests: the worker just drops this, freeing it off the audio thread.
	std::shared_ptr<const void> garbage;

	BakeRequest* next{};
	std::shared_ptr<BakeRequest> self;
};

class WorkSemaphore
{
public:
#if defined(__APPLE__)
	WorkSemaphore() : s_(dispatch_semaphore_create(0)) {}
	~WorkSemaphore()
	{
#if !__has_feature(objc_arc)
		dispatch_release(s_);
#endif
	}
	void release() { dispatch_semaphore_signal(s_); }
	void acquire() { dispatch_semaphore_wait(s_, DISPATCH_TIME_FOREVER); }
#else
	WorkSemaphore() = default;
	void release() { s_.release(); }
	void acquire() { s_.acquire(); }
#endif
	WorkSemaphore(const WorkSemaphore&) = delete;
	WorkSemaphore& operator=(const WorkSemaphore&) = delete;

private:
#if defined(__APPLE__)
	dispatch_semaphore_t s_;
#else
	std::counting_semaphore<> s_{0};
#endif
};

// One worker thread draining a lock-free queue; enqueue() never blocks the audio thread.
// Joined when the last instance releases it, never during static destruction (loader-lock deadlock).
class BakeLoader
{
public:
	BakeLoader();
	~BakeLoader();

	void enqueue(std::shared_ptr<BakeRequest> request);
	void dispose(std::shared_ptr<const void> garbage);

private:
	void run();

	std::atomic<BakeRequest*> queueHead_{nullptr}; // LIFO; worker reverses to FIFO.
	WorkSemaphore             workAvailable_;
	std::atomic<bool>         stop_{false};
	std::thread               worker_;
};

std::shared_ptr<BakeLoader> bakeLoader();
}
