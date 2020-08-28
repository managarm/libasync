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

TEST(Algorithm, Sequence) {
	async::run_queue rq;
	async::queue_scope qs{&rq};

	int steps[4] = {0, 0, 0, 0};
	int v = async::run([&]() -> async::result<int> {
		int i = 0;
		co_return co_await async::sequence(
			[&]() -> async::result<void> {
				steps[0] = i;
				i++;
				co_return;
			}(),
			[&]() -> async::result<void> {
				steps[1] = i;
				i++;
				co_return;
			}(),
			[&]() -> async::result<void> {
				steps[2] = i;
				i++;
				co_return;
			}(),
			[&]() -> async::result<int> {
				steps[3] = i;
				i++;
				co_return i;
			}()
		);
	}(), async::current_queue);
	ASSERT_EQ(v, 4);

	for (int i = 0; i < 4; i++)
		ASSERT_EQ(steps[i], i);
}
