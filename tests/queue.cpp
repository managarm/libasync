#include <new>

#include <async/queue.hpp>
#include <async/result.hpp>
#include <gtest/gtest.h>

#include <frg/std_compat.hpp>

TEST(Queue, PutGet) {
	async::queue<int, frg::stl_allocator> q;
	q.put(42);
	q.put(21);
	auto v1 = async::run(q.async_get());
	auto v2 = async::run(q.async_get());
	ASSERT_EQ(v1, 42);
	ASSERT_EQ(v2, 21);
}

TEST(Queue, Cancel) {
	async::cancellation_event ce;
	async::queue<int, frg::stl_allocator> q;
	ce.cancel();
	auto v1 = async::run(q.async_get(ce));
	ASSERT_FALSE(v1);
}
