#include <new>

#include <async/queue.hpp>
#include <async/result.hpp>
#include <async/algorithm.hpp>
#include <gtest/gtest.h>

TEST(Race, QueueRaceAndCancel) {
	async::run(async::race_and_cancel(
		[] (async::cancellation_token) -> async::result<void> { co_return; },
		[] (async::cancellation_token) -> async::result<void> { co_return; }
	));

	ASSERT_TRUE(true);
}
