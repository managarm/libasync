#include <functional>
#include <new>
#include <optional>

#include <async/queue.hpp>
#include <async/result.hpp>
#include <async/algorithm.hpp>
#include <gtest/gtest.h>

namespace {

// Completes when the test calls complete(), and calls on_cancel when it is cancelled.
// Unlike a coroutine, its operation keeps the cancellation callback registered after completing.
struct manual_sender {
	using value_type = void;

	template<typename Receiver>
	struct operation {
		void start() {
			*complete = [this] { async::execution::set_value(receiver); };
			if(on_cancel)
				callback.emplace(token, [this] { (*on_cancel)(); });
		}

		std::function<void()> *complete;
		async::cancellation_token token;
		std::function<void()> *on_cancel;
		Receiver receiver;
		std::optional<async::cancellation_callback<std::function<void()>>> callback = {};
	};

	template<typename Receiver>
	friend operation<Receiver> connect(manual_sender s, Receiver r) {
		return {s.complete, s.token, s.on_cancel, std::move(r)};
	}

	std::function<void()> *complete;
	async::cancellation_token token = {};
	std::function<void()> *on_cancel = nullptr;
};

struct flag_receiver {
	void set_value() {
		*flag = true;
	}

	bool *flag;
};

} // namespace

TEST(Race, QueueRaceAndCancel) {
	async::run(async::race_and_cancel(
		[] (async::cancellation_token) -> async::result<void> { co_return; },
		[] (async::cancellation_token) -> async::result<void> { co_return; }
	));

	ASSERT_TRUE(true);
}

// While the first branch cancels the others, they complete (here: synchronously from the second
// branch's cancellation callback). The race must not complete before the cancelling is done,
// since completing it allows the operation to be destructed.
TEST(Race, RaceAndCancelCompletesAfterCancellingAllBranches) {
	std::function<void()> complete_first, complete_second, complete_third;
	bool done = false;
	bool done_during_cancel = false;

	std::function<void()> on_cancel = [&] {
		complete_third();
		complete_second();
		done_during_cancel = done;
	};

	auto op = async::execution::connect(
		async::race_and_cancel(
			[&] (async::cancellation_token) { return manual_sender{&complete_first}; },
			[&] (async::cancellation_token c) {
				return manual_sender{&complete_second, c, &on_cancel};
			},
			[&] (async::cancellation_token) { return manual_sender{&complete_third}; }
		),
		flag_receiver{&done}
	);
	async::execution::start(op);

	complete_first();
	ASSERT_FALSE(done_during_cancel);
	ASSERT_TRUE(done);
}

// The first branch to complete is not cancelled, so its callback is still registered when the
// operation is destructed. If the events were destructed first, the assertion in
// ~cancellation_event() would fail.
TEST(Race, RaceAndCancelDestructsBranchesBeforeTheirEvents) {
	std::function<void()> complete_first, complete_second;
	std::function<void()> on_cancel = [] { };
	bool done = false;

	{
		auto op = async::execution::connect(
			async::race_and_cancel(
				[&] (async::cancellation_token c) {
					return manual_sender{&complete_first, c, &on_cancel};
				},
				[&] (async::cancellation_token) { return manual_sender{&complete_second}; }
			),
			flag_receiver{&done}
		);
		async::execution::start(op);

		complete_first();
		complete_second();
		ASSERT_TRUE(done);
	}
}
