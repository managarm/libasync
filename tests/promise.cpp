#include <new>

#include <async/promise.hpp>
#include <gtest/gtest.h>

struct stl_allocator {
	void *allocate(size_t size) {
		return operator new(size);
	}

	void deallocate(void *p, size_t) {
		return operator delete(p);
	}
};


TEST(Promise, VoidType) {
	async::run_queue rq;
	async::queue_scope qs{&rq};

	async::future<void, stl_allocator> future;
	{
		async::promise<void, stl_allocator> promise;
		future = promise.get_future();

		promise.set_value();
	}

	async::run(future.get(), async::current_queue);

	ASSERT_TRUE(true);
}

TEST(Promise, IntType) {
	async::run_queue rq;
	async::queue_scope qs{&rq};

	async::future<int, stl_allocator> future;
	{
		async::promise<int, stl_allocator> promise;
		future = promise.get_future();

		promise.set_value(3);
	}

	auto res = *async::run(future.get(), async::current_queue);

	ASSERT_EQ(res, 3);
}

TEST(Promise, NonCopyableType) {
	async::run_queue rq;
	async::queue_scope qs{&rq};

	struct non_copy {
		non_copy(int i) : i{i} { }
		non_copy(const non_copy &) = delete;
		non_copy(non_copy &&) = default;
		non_copy &operator=(const non_copy &) = delete;
		non_copy &operator=(non_copy &&) = default;

		int i;
	};

	async::future<non_copy, stl_allocator> future;
	{
		async::promise<non_copy, stl_allocator> promise;
		future = promise.get_future();

		promise.set_value(non_copy{3});
	}

	auto &res = *async::run(future.get(), async::current_queue);

	ASSERT_EQ(res.i, 3);
}

TEST(Promise, MultipleFutures) {
	async::run_queue rq;
	async::queue_scope qs{&rq};

	async::future<int, stl_allocator> f1, f2, f3;
	{
		async::promise<int, stl_allocator> promise;
		f1 = promise.get_future();
		f2 = promise.get_future();
		f3 = promise.get_future();

		promise.set_value(3);
	}

	auto p1 = async::run(f1.get(), async::current_queue);
	auto p2 = async::run(f2.get(), async::current_queue);
	auto p3 = async::run(f3.get(), async::current_queue);

	ASSERT_EQ(p1, p2);
	ASSERT_EQ(p1, p3);

	ASSERT_EQ(*p1, 3);
}
