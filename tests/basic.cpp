#include <async/basic.hpp>
#include <async/result.hpp>
#include <gtest/gtest.h>

TEST(Basic, CallCoroutine) {
	async::run_queue rq;
	async::queue_scope qs{&rq};

	int v = async::run([] (int *p) -> async::result<int> {
		co_return 42;
	}(&v), async::current_queue);
	ASSERT_EQ(v, 42);
}
