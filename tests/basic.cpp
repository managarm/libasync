#include <async/basic.hpp>
#include <async/result.hpp>
#include <async/queue.hpp>
#include <gtest/gtest.h>

TEST(Basic, CallCoroutine) {
	async::run_queue rq;
	async::queue_scope qs{&rq};

	int v = async::run([] (int *p) -> async::result<int> {
		co_return 42;
	}(&v), async::current_queue);
	ASSERT_EQ(v, 42);
}

TEST(Basic, Detached) {
	async::run_queue rq;
	async::queue_scope qs{&rq};

	struct {
		async::detached detached_work() {
			if (co_await q.async_get() == 1) ok_1 = true;
			if (co_await q.async_get() == 2) ok_2 = true;
			if (co_await q.async_get() == 3) ok_3 = true;
		}

		async::result<void> regular_work() {
			q.put(1);
			q.put(2);
			q.put(3);

			co_return;
		}

		async::queue<int, frg::stl_allocator> q;
		bool ok_1 = false, ok_2 = false, ok_3 = false;
	} obj;

	obj.detached_work();
	async::run(obj.regular_work(), async::current_queue);

	ASSERT_TRUE(obj.ok_1);
	ASSERT_TRUE(obj.ok_2);
	ASSERT_TRUE(obj.ok_3);
}
