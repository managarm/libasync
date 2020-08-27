#include <async/basic.hpp>
#include <async/result.hpp>
#include <async/algorithm.hpp>
#include <gtest/gtest.h>

TEST(Algorithm, Let) {
	async::run_queue rq;
	async::queue_scope qs{&rq};

	int v = async::run([]() -> async::result<int> {
		co_return co_await async::let(
			[]() -> int { return 21; },
			[](int &ref) -> async::result<int> {
				co_return ref * 2;
			}
		);
	}(), async::current_queue);
	ASSERT_EQ(v, 42);
}
