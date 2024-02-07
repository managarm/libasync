#include <memory>
#include <async/post-ack.hpp>
#include <gtest/gtest.h>

TEST(PostAck, IntType) {
	async::post_ack_mechanism<int> mech;

	int ok1_ctr = 0, ok2_ctr = 0;

	auto producer = [] (async::post_ack_mechanism<int> &mech) -> async::detached {
		co_await mech.post(1);
		co_await mech.post(2);
	};

	auto consumer = [&] (async::post_ack_mechanism<int> &mech) -> async::detached {
		async::post_ack_agent<int> agent;
		agent.attach(&mech);

		auto handle = co_await agent.poll();
		if (*handle == 1) ok1_ctr++;
		handle.ack();

		handle = co_await agent.poll();
		if (*handle == 2) ok2_ctr++;
		handle.ack();

		agent.detach();
	};

	consumer(mech);
	consumer(mech);
	consumer(mech);

	producer(mech);

	ASSERT_EQ(ok1_ctr, 3);
	ASSERT_EQ(ok2_ctr, 3);
}

TEST(PostAck, ImmovableType) {
	async::post_ack_mechanism<std::unique_ptr<int>> mech;

	int ok1_ctr = 0, ok2_ctr = 0;

	auto producer = [] (async::post_ack_mechanism<std::unique_ptr<int>> &mech) -> async::detached {
		co_await mech.post(std::make_unique<int>(1));
		co_await mech.post(std::make_unique<int>(2));
	};

	auto consumer = [&] (async::post_ack_mechanism<std::unique_ptr<int>> &mech) -> async::detached {
		async::post_ack_agent<std::unique_ptr<int>> agent;
		agent.attach(&mech);

		auto handle = co_await agent.poll();
		if (*handle != nullptr) ok1_ctr++;
		if (**handle == 1) ok1_ctr++;
		handle.ack();

		handle = co_await agent.poll();
		if (*handle != nullptr) ok2_ctr++;
		if (**handle == 2) ok2_ctr++;
		handle.ack();

		agent.detach();
	};

	consumer(mech);
	consumer(mech);
	consumer(mech);
	consumer(mech);

	producer(mech);

	ASSERT_EQ(ok1_ctr, 8);
	ASSERT_EQ(ok2_ctr, 8);
}
