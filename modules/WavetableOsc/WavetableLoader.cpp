#include "WavetableLoader.h"

#include <mutex>

std::shared_ptr<WavetableLoader> wavetableLoader()
{
	// Weak-cached so the loader (and its worker thread) lives exactly as long as the set of
	// module instances using it: constructed by the first open(), joined when the last
	// instance is destroyed. Never reaches static destruction with a live thread.
	static std::mutex m;
	static std::weak_ptr<WavetableLoader> cached;

	std::scoped_lock lock{m};
	auto p = cached.lock();
	if (!p)
	{
		p = std::make_shared<WavetableLoader>();
		cached = p;
	}
	return p;
}

WavetableLoader::WavetableLoader()
{
	worker_ = std::thread(&WavetableLoader::run, this);
}

WavetableLoader::~WavetableLoader()
{
	// Runs when the last referencing module instance is destroyed (plugin teardown on a
	// normal thread) - never under the OS loader lock, so joining is safe.
	stop_.store(true, std::memory_order_release);
	workAvailable_.release();
	if (worker_.joinable())
		worker_.join();

	// Free anything still queued. No producers remain: every enqueue() caller held a
	// shared_ptr to us, and this destructor means the last one is gone.
	auto* p = queueHead_.exchange(nullptr, std::memory_order_acquire);
	while (p)
	{
		auto* next = p->next;
		p->self.reset(); // may destroy *p - read `next` first.
		p = next;
	}
}

void WavetableLoader::enqueue(std::shared_ptr<WavetableLoadRequest> request)
{
	// Lock-free push (Treiber stack). The queue's reference lives in request->self, so the
	// raw `next` links stay valid while queued. Called from the audio thread: no mutex is
	// shared with the worker, so a preempted worker can never stall the audio callback.
	auto* p = request.get();
	p->self = std::move(request);
	p->next = queueHead_.load(std::memory_order_relaxed);
	while (!queueHead_.compare_exchange_weak(p->next, p,
		std::memory_order_release, std::memory_order_relaxed))
	{
	}

	workAvailable_.release(); // never blocks.
}

void WavetableLoader::dispose(std::shared_ptr<void> garbage)
{
	if (!garbage)
		return;

	auto tomb = std::make_shared<WavetableLoadRequest>();
	tomb->garbage = std::move(garbage);
	enqueue(std::move(tomb));
}

void WavetableLoader::run()
{
	while (!stop_.load(std::memory_order_acquire))
	{
		workAvailable_.acquire();

		// Grab everything pushed so far and restore FIFO order (the stack is LIFO). Extra
		// semaphore counts just produce empty grabs - harmless.
		auto* p = queueHead_.exchange(nullptr, std::memory_order_acquire);
		WavetableLoadRequest* fifo = nullptr;
		while (p)
		{
			auto* next = p->next;
			p->next = fifo;
			fifo = p;
			p = next;
		}

		while (fifo)
		{
			auto* next = fifo->next;
			auto request = std::move(fifo->self); // dropping this below may destroy the request.
			fifo = next;

			// Tombstones and superseded requests: just drop the reference - the whole point
			// is that the (possibly multi-MB) pointee gets freed here, on the worker.
			if (request->garbage || request->cancelled.load(std::memory_order_acquire))
				continue;

			request->result = wavetableCache().getOrLoad(request->uri, request->sampleRate);
			request->ready.store(true, std::memory_order_release);
		}
	}
}
