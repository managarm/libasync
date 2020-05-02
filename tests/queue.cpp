#include <new>

#include <async/queue.hpp>
#include <async/result.hpp>
#include <gtest/gtest.h>

struct stl_allocator {
	void *allocate(size_t size) {
		return operator new(size);
	}

	void deallocate(void *p, size_t) {
		return operator delete(p);
	}
};

TEST(Queue, PutGet) {
	async::run_queue rq;
	async::queue_scope qs{&rq};

	async::queue<int, stl_allocator> q;
	q.put(42);
	q.put(21);
	auto v1 = async::run(q.async_get(), async::current_queue);
	auto v2 = async::run(q.async_get(), async::current_queue);
	ASSERT_EQ(v1, 42);
	ASSERT_EQ(v2, 21);
}

TEST(Queue, Cancel) {
	async::run_queue rq;
	async::queue_scope qs{&rq};

	async::cancellation_event ce;
	async::queue<int, stl_allocator> q;
	ce.cancel();
	auto v1 = async::run(q.async_get(ce), async::current_queue);
	ASSERT_FALSE(v1);
}
