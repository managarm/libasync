#include <atomic>
#include <memory>
#include <thread>
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

// Stress test for the race between completion and cancellation of a poll operation;
// resuming (and thereby destructing) the operation must not overlap with the racing
// thread's accesses inside cancellation_resolver (cf. managarm/managarm#1509).
// Best run under ASan/TSan to detect regressions.
TEST(PostAck, PollCancelRace) {
	for (int round = 0; round < 10000; ++round) {
		async::post_ack_mechanism<int> mech;
		async::post_ack_agent<int> agent;
		agent.attach(&mech);
		async::cancellation_event ce;

		std::atomic<bool> consumerDone{false};
		std::atomic<bool> producerDone{false};

		auto consumer = [&] () -> async::detached {
			{
				auto handle = co_await agent.poll(ce);
				if (handle)
					handle.ack();
			}
			consumerDone.store(true, std::memory_order_release);
		};

		auto producer = [&] () -> async::detached {
			co_await mech.post(round);
			producerDone.store(true, std::memory_order_release);
		};

		std::atomic<int> ready{0};
		auto spinUntilReady = [&] {
			ready.fetch_add(1, std::memory_order_relaxed);
			while (ready.load(std::memory_order_relaxed) != 3)
				;
		};
		std::thread poller{[&] { spinUntilReady(); consumer(); }};
		std::thread poster{[&] { spinUntilReady(); producer(); }};
		std::thread canceller{[&] { spinUntilReady(); ce.cancel(); }};
		poller.join();
		poster.join();
		canceller.join();

		while (!consumerDone.load(std::memory_order_acquire))
			std::this_thread::yield();

		// If the poll was cancelled, detach() acks the outstanding post.
		agent.detach();

		while (!producerDone.load(std::memory_order_acquire))
			std::this_thread::yield();
	}
}
