#include <new>

#include <async/doorbell.hpp>
#include <async/queue.hpp>
#include <async/result.hpp>
#include <async/algorithm.hpp>
#include <gtest/gtest.h>

struct stl_allocator {
	void *allocate(size_t size) {
		return operator new(size);
	}

	void deallocate(void *p, size_t) {
		return operator delete(p);
	}
};

TEST(Race, QueueRaceAndCancel) {
	async::run_queue rq;
	async::queue_scope qs{&rq};

	async::queue<int, stl_allocator> p1, p2;
	p1.put(42);

	async::run(async::race_and_cancel(
		[&](async::cancellation_token ev) -> async::result<void> {
			co_await p1.async_get(ev);
			co_return;
		},
		[&](async::cancellation_token ev) -> async::result<void> {
			co_await p2.async_get(ev);
			co_return;
		}
	), async::current_queue);
	ASSERT_TRUE(true);
}
