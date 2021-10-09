#include <new>

#include <async/promise.hpp>
#include <gtest/gtest.h>
#include <frg/std_compat.hpp>

TEST(Promise, VoidType) {
	async::future<void, frg::stl_allocator> future;
	{
		async::promise<void, frg::stl_allocator> promise;
		future = promise.get_future();

		promise.set_value();
	}

	async::run(future.get());

	ASSERT_TRUE(true);
}

TEST(Promise, IntType) {
	async::future<int, frg::stl_allocator> future;
	{
		async::promise<int, frg::stl_allocator> promise;
		future = promise.get_future();

		promise.set_value(3);
	}

	auto res = *async::run(future.get());

	ASSERT_EQ(res, 3);
}

TEST(Promise, NonCopyableType) {
	struct non_copy {
		non_copy(int i) : i{i} { }
		non_copy(const non_copy &) = delete;
		non_copy(non_copy &&) = default;
		non_copy &operator=(const non_copy &) = delete;
		non_copy &operator=(non_copy &&) = default;

		int i;
	};

	async::future<non_copy, frg::stl_allocator> future;
	{
		async::promise<non_copy, frg::stl_allocator> promise;
		future = promise.get_future();

		promise.set_value(non_copy{3});
	}

	auto &res = *async::run(future.get());

	ASSERT_EQ(res.i, 3);
}

TEST(Promise, MultipleFutures) {
	async::future<int, frg::stl_allocator> f1, f2, f3;
	{
		async::promise<int, frg::stl_allocator> promise;
		f1 = promise.get_future();
		f2 = promise.get_future();
		f3 = promise.get_future();

		promise.set_value(3);
	}

	auto p1 = async::run(f1.get());
	auto p2 = async::run(f2.get());
	auto p3 = async::run(f3.get());

	ASSERT_EQ(p1, p2);
	ASSERT_EQ(p1, p3);

	ASSERT_EQ(*p1, 3);
}
