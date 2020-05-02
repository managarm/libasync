#ifndef LIBASYNC_QUEUE_HPP
#define LIBASYNC_QUEUE_HPP

#include <async/result.hpp>
#include <async/doorbell.hpp>
#include <async/cancellation.hpp>
#include <frg/list.hpp>
#include <frg/optional.hpp>

namespace async {

template<typename T, typename Allocator>
struct queue {
	void put(T item) {
		emplace(std::move(item));
	}

	template<typename... Ts>
	void emplace(Ts&&... arg) {
		_queue.emplace_back(std::forward<Ts>(arg)...);
		_doorbell.ring();
	}

	async::result<frg::optional<T>> async_get(async::cancellation_token token = {}) {
		while (_queue.empty() && !token.is_cancellation_requested())
			co_await _doorbell.async_wait(token);

		if (token.is_cancellation_requested())
			co_return frg::null_opt;

		auto v = std::move(_queue.front());
		_queue.pop_front();

		co_return v;
	}

private:
	doorbell _doorbell;
	frg::list<T, Allocator> _queue;
};

}

#endif // LIBASYNC_QUEUE_HPP
