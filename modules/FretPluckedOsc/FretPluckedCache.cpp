#include "FretPluckedCache.h"

#include <algorithm>
#include <chrono>

namespace fret_plucked
{
TableCache& tableCache()
{
	static TableCache instance;
	return instance;
}

std::shared_ptr<const Table> TableCache::getOrBake(const Params& params, float sampleRate, const std::atomic<bool>* cancelled)
{
	const Key key{params, sampleRate};

	std::unique_lock lock{mtx_};

	for (;;)
	{
		entries_.erase(std::remove_if(entries_.begin(), entries_.end(), [](const Entry& e) { return e.table.expired(); }), entries_.end());

		for (const auto& e : entries_)
		{
			if (e.key == key)
			{
				if (auto alive = e.table.lock())
					return alive;
			}
		}

		if (std::find(building_.begin(), building_.end(), key) == building_.end())
			break;

		buildDone_.wait(lock);
	}

	building_.push_back(key);
	lock.unlock();

	std::shared_ptr<const Table> table;
	try
	{
		table = bake(params, sampleRate, cancelled);
	}
	catch (...)
	{
		lock.lock();
		building_.erase(std::find(building_.begin(), building_.end(), key));
		buildDone_.notify_all();
		throw;
	}

	lock.lock();
	if (table)
		entries_.push_back({key, table});
	building_.erase(std::find(building_.begin(), building_.end(), key));
	buildDone_.notify_all();
	return table;
}

std::shared_ptr<BakeLoader> bakeLoader()
{
	static std::mutex m;
	static std::weak_ptr<BakeLoader> cached;

	std::scoped_lock lock{m};
	auto p = cached.lock();
	if (!p)
	{
		p = std::make_shared<BakeLoader>();
		cached = p;
	}
	return p;
}

BakeLoader::BakeLoader()
{
	worker_ = std::thread(&BakeLoader::run, this);
}

BakeLoader::~BakeLoader()
{
	stop_.store(true, std::memory_order_release);
	workAvailable_.release();
	if (worker_.joinable())
		worker_.join();

	auto* p = queueHead_.exchange(nullptr, std::memory_order_acquire);
	while (p)
	{
		auto* next = p->next;
		p->self.reset(); // may destroy *p - read `next` first.
		p = next;
	}
}

void BakeLoader::enqueue(std::shared_ptr<BakeRequest> request)
{
	auto* p = request.get();
	p->self = std::move(request);
	p->next = queueHead_.load(std::memory_order_relaxed);
	while (!queueHead_.compare_exchange_weak(p->next, p,
		std::memory_order_release, std::memory_order_relaxed))
	{
	}

	workAvailable_.release();
}

void BakeLoader::dispose(std::shared_ptr<const void> garbage)
{
	if (!garbage)
		return;

	auto tomb = std::make_shared<BakeRequest>();
	tomb->garbage = std::move(garbage);
	enqueue(std::move(tomb));
}

void BakeLoader::run()
{
	while (!stop_.load(std::memory_order_acquire))
	{
		workAvailable_.acquire();

		auto* p = queueHead_.exchange(nullptr, std::memory_order_acquire);
		BakeRequest* fifo = nullptr;
		bool hasWork = false;
		while (p)
		{
			auto* next = p->next;
			p->next = fifo;
			fifo = p;
			hasWork = hasWork || !p->garbage;
			p = next;
		}

		// Debounce: a knob being dragged supersedes most requests before they start.
		if (hasWork)
			std::this_thread::sleep_for(std::chrono::milliseconds(60));

		while (fifo)
		{
			auto* next = fifo->next;
			auto request = std::move(fifo->self); // dropping this below may destroy the request.
			fifo = next;

			if (request->garbage || request->cancelled.load(std::memory_order_acquire))
				continue;

			request->result = tableCache().getOrBake(request->params, request->sampleRate, &request->cancelled);
			request->ready.store(true, std::memory_order_release);
		}
	}
}
} // namespace fret_plucked
