#ifndef LIBASYNC_QUEUE_HPP
#define LIBASYNC_QUEUE_HPP

#include <async/result.hpp>
#include <async/doorbell.hpp>
#include <queue>

namespace async {

template<typename T>
struct queue {
	void put(T item) {
		_queue.push(item);
		_doorbell.ring();
	}

	async::result<T> async_get() {
		while (_queue.empty())
			co_await _doorbell.async_wait();

		auto v = _queue.front();
		_queue.pop();

		co_return v;
	}

private:
	doorbell _doorbell;
	std::queue<T> _queue;
};

}

#endif // LIBASYNC_QUEUE_HPP
