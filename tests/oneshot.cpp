#include <async/basic.hpp>
#include <async/oneshot-event.hpp>
#include <gtest/gtest.h>

TEST(OneshotPrimitive, RaiseBeforeWait) {
	async::oneshot_primitive ev;
	bool done = false;

	auto coro = [] (async::oneshot_primitive *ev_p, bool *done_p) -> async::detached {
		co_await ev_p->wait();
		*done_p = true;
	};

	ev.raise();
	ASSERT_FALSE(done);
	coro(&ev, &done);
	ASSERT_TRUE(done);
}

TEST(OneshotPrimitive, WaitBeforeRaise) {
	async::oneshot_primitive ev;
	bool done = false;

	auto coro = [] (async::oneshot_primitive *ev_p, bool *done_p) -> async::detached {
		co_await ev_p->wait();
		*done_p = true;
	};

	coro(&ev, &done);
	ASSERT_FALSE(done);
	ev.raise();
	ASSERT_TRUE(done);
}
