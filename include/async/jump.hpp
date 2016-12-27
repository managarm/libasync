#ifndef ASYNC_JUMP_HPP
#define ASYNC_JUMP_HPP

#include <mutex>
#include <deque>

#include <async/result.hpp>

namespace async {

struct jump : private awaitable<void> {
	jump()
	: _done(false) { }

	void trigger() {
		std::deque<callback<void()>> queue;
		{
			std::lock_guard<std::mutex> lock(_mutex);
			assert(!_done);
			_done = true;
			queue = std::move(_queue);
		}

		for(auto it = queue.begin(); it != queue.end(); ++it)
			(*it)();
	}

	result<void> async_wait() {
		return result<void>{this};
	}

private:
	void then(callback<void()> awaiter) override {
		{
			std::lock_guard<std::mutex> lock(_mutex);
			if(!_done) {
				_queue.push_back(awaiter);
				return;
			}
		}

		awaiter();
	}

	void detach() override {
		// It makes so sense to call async_wait() if you do not await
		// on the result. TODO: Throw an exception?
	}

	std::mutex _mutex;
	bool _done;
	std::deque<callback<void()>> _queue;
};

} // namespace async

#endif // ASYNC_JUMP_HPP
