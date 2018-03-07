#ifndef ASYNC_JUMP_HPP
#define ASYNC_JUMP_HPP

#include <mutex>
#include <deque>

#include <async/result.hpp>
#include <async/doorbell.hpp>

namespace async {

struct jump {
	jump()
	: _done(false) { }

	void trigger() {
		{
			std::lock_guard<std::mutex> lock(_mutex);
			_done = true;
		}
		_bell.ring();
	}

	// TODO: This emulation is a bad idea from a performance point-of-view. Refactor it.
	result<void> async_wait() {
		auto result = _bell.async_wait();
		{
			std::lock_guard<std::mutex> lock(_mutex);
			if(!_done)
				return std::move(result);
		}
		_bell.ring();
		return std::move(result);
	}

	void reset() {
		// TODO: We do not need the lock for exclusion.
		// Do we need it for the memory barrier?
		std::lock_guard<std::mutex> lock(_mutex);
		_done = false;
	}

private:
	std::mutex _mutex;
	bool _done;
	doorbell _bell;
};

} // namespace async

#endif // ASYNC_JUMP_HPP
