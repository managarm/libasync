#pragma once

#include <atomic>
#include <concepts>

#include <async/platform-support.hpp>
#include <frg/list.hpp>
#include <frg/mutex.hpp>

namespace async {

struct run_queue;

#ifndef LIBASYNC_CUSTOM_PLATFORM

// This class uniquely identifies a thread that run queues can be affine to.
// Run queues only compare this pointer to the current thread's context.
// They never dereference it from another thread.
// Run queues must not outlive their context.
struct run_queue_context {
	friend struct run_queue;

	run_queue_context() = default;

	run_queue_context(const run_queue_context &) = delete;

	run_queue_context &operator= (const run_queue_context &) = delete;

	// Queue that coroutines become affine to if no queue is determined otherwise.
	run_queue *default_queue() {
		return _defaultRq;
	}

	void set_default_queue(run_queue *rq);

	// Unbinds the default queue, e.g., before the queue is destructed.
	void reset_default_queue() {
		assert(_defaultRq && "context has no default run_queue");
		_defaultRq = nullptr;
	}

private:
	run_queue *_defaultRq{nullptr};
};

namespace detail {
	inline thread_local run_queue_context tls_run_queue_context;
}

inline run_queue_context *current_run_queue_context() {
	return &detail::tls_run_queue_context;
}

inline run_queue *get_default_queue() {
	return detail::tls_run_queue_context.default_queue();
}

struct run_queue_item {
	friend struct run_queue;

	run_queue_item() = default;

	run_queue_item(const run_queue_item &) = delete;

	run_queue_item &operator= (const run_queue_item &) = delete;

	void setup(void (*run)(run_queue_item *)) {
		_run = run;
	}

private:
	void (*_run)(run_queue_item *) = nullptr;
	frg::default_list_hook<run_queue_item> _hook;
};

// Run queue for asynchronous work.
// This is a building block for async runtimes. Runtimes are not part of libasync.
// Usage:
// - The runtime is responsible for blocking the current thread.
// - The runtime must override wakeup() to wake a block thread.
//   This is called when cross-thread work is posted.
// - Posting work from the run queue's own thread does not invoke wakeup().
//   Thus, the runtime always needs to re-evaluate check() before blocking.
//   Note that if a thread owns multiple run queues, it either needs to run check()
//   on all of them before blocking or it needs to remember which run queue caused the wakeup().
// - Memory ordering: check() acts as a relaxed load w.r.t. cross-thread posts.
//   Runtimes need to ensure that their wakeup() -> check() code path
//   does at least an acquire-relase barrier to guarantee correctness.
struct run_queue {
	// Queues are affine to the thread that constructs them unless a context is given.
	run_queue(run_queue_context *context = current_run_queue_context())
	: _context{context} { }

	run_queue_context *context() {
		return _context;
	}

	// Returns true if an item posted using post() would run immediately.
	bool immediately_runnable() {
		return _context == current_run_queue_context() && _inRun;
	}

	void post(run_queue_item *item) {
		assert(item->_run && "run_queue_item must be set up before post()");

		if(_context == current_run_queue_context()) {
			// If an item posts another item, we proceed in LIFO order,
			// i.e., in the same order that a call stack would also proceed.
			if(_inRun)
				_pending.push_front(item);
			else
				_pending.push_back(item);
			return;
		}

		bool invokeWakeup;
		{
			frg::unique_lock lock{_mutex};

			invokeWakeup = _lockedQueue.empty();
			_lockedQueue.push_back(item);
			_lockedPosted.store(true, std::memory_order_relaxed);
		}
		if(invokeWakeup)
			wakeup();
	}

	bool check() {
		return !_pending.empty() || _lockedPosted.load(std::memory_order_relaxed);
	}

	void run_iteration() {
		assert(_context == current_run_queue_context());

		if(_lockedPosted.load(std::memory_order_relaxed)) {
			frg::unique_lock lock{_mutex};

			_pending.splice(_pending.end(), _lockedQueue);
			_lockedPosted.store(false, std::memory_order_relaxed);
		}

		// run_iteration() can be entered recursively from work_queue_item::_run(),
		// i.e., an item drives a nested async runtime.
		// The nested iteration then drains items on behalf of the outer one.
		bool nested = _inRun;
		_inRun = true;
		while(!_pending.empty()) {
			auto item = _pending.pop_front();
			item->_run(item);
		}
		_inRun = nested;
	}

protected:
	// Called on the empty-to-non-empty transition of the cross-thread lane.
	// May be called concurrently from multiple threads.
	virtual void wakeup() = 0;

	~run_queue() = default;

private:
	using item_list = frg::intrusive_list<
		run_queue_item,
		frg::locate_member<
			run_queue_item,
			frg::default_list_hook<run_queue_item>,
			&run_queue_item::_hook
		>
	>;

	run_queue_context *_context;

	// Items posted from the owning thread; only accessed from that thread.
	item_list _pending;

	bool _inRun = false;

	// Items posted from other threads.
	platform::mutex _mutex;
	std::atomic<bool> _lockedPosted{false};
	item_list _lockedQueue;
};

inline void run_queue_context::set_default_queue(run_queue *rq) {
	assert(rq->context() == this && "run_queue is not affine to this context");
	assert(!_defaultRq && "context already has a default run_queue");
	_defaultRq = rq;
}

#else // LIBASYNC_CUSTOM_PLATFORM

// Run queues require a hosted platform; without one, no queue is ever current.
inline run_queue *get_default_queue() {
	return nullptr;
}

#endif // LIBASYNC_CUSTOM_PLATFORM

template<typename E>
concept has_get_run_queue = requires(E env) {
	{ env.get_run_queue() } -> std::same_as<run_queue *>;
};

// Resolves the run queue that a coroutine should be affine to: the receiver's
// environment takes precedence; otherwise, fall back to the current thread's queue.
template<typename E>
run_queue *run_queue_from_env(E env) {
	if constexpr (has_get_run_queue<E>) {
		if(auto rq = env.get_run_queue(); rq)
			return rq;
	}
	return get_default_queue();
}

} // namespace async
