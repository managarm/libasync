#ifndef LIBASYNC_QUEUE_HPP
#define LIBASYNC_QUEUE_HPP

#include <async/result.hpp>
#include <async/doorbell.hpp>
#include <async/cancellation.hpp>
#include <queue>
#include <optional>

namespace async {

template<typename T>
struct queue {
	void put(T item) {
		_queue.push(std::move(item));
		_doorbell.ring();
	}

	template<typename... Ts>
	void emplace(Ts&&... arg) {
		_queue.emplace(std::forward<Ts>(arg)...);
		_doorbell.ring();
	}

	async::result<std::optional<T>> async_get(async::cancellation_token token = {}) {
		while (_queue.empty() && !token.is_cancellation_requested())
			co_await _doorbell.async_wait(token);

		if (token.is_cancellation_requested())
			co_return std::nullopt;

		auto v = std::move(_queue.front());
		_queue.pop();

		co_return v;
	}

private:
	doorbell _doorbell;
	std::queue<T> _queue;
};

}

#endif // LIBASYNC_QUEUE_HPP
