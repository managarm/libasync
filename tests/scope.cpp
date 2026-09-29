#include <async/basic.hpp>
#include <async/oneshot-event.hpp>
#include <async/result.hpp>
#include <async/scope.hpp>
#include <gtest/gtest.h>

namespace {

async::result<void> waitingChild(async::oneshot_event *ev, int *started, int *finished) {
	++*started;
	co_await ev->wait();
	++*finished;
}

} // namespace

TEST(DynamicScope, StartsChildrenImmediately) {
	async::oneshot_event ev;
	int started = 0;
	int finished = 0;
	bool done = false;

	async::detach(async::dynamic_scope<async::result<void>>(frg::stl_allocator{},
		[&] (auto &spawn) {
			spawn(waitingChild(&ev, &started, &finished));
			EXPECT_EQ(started, 1);
			spawn(waitingChild(&ev, &started, &finished));
			EXPECT_EQ(started, 2);
		}
	), [&] { done = true; });

	ASSERT_FALSE(done);
	ev.raise();
	ASSERT_EQ(finished, 2);
	ASSERT_TRUE(done);
}

TEST(DynamicScope, DeferredStartsChildrenAfterFill) {
	async::oneshot_event ev;
	int started = 0;
	int finished = 0;
	bool done = false;

	async::detach(async::dynamic_deferred_scope<async::result<void>>(frg::stl_allocator{},
		[&] (auto &spawn) {
			spawn(waitingChild(&ev, &started, &finished));
			spawn(waitingChild(&ev, &started, &finished));
			EXPECT_EQ(started, 0);
		}
	), [&] { done = true; });

	ASSERT_EQ(started, 2);
	ASSERT_FALSE(done);
	ev.raise();
	ASSERT_EQ(finished, 2);
	ASSERT_TRUE(done);
}

TEST(DynamicScope, InlineCompletion) {
	int n = 0;
	bool done = false;

	async::detach(async::dynamic_deferred_scope<async::result<void>>(frg::stl_allocator{},
		[&] (auto &spawn) {
			for(int i = 0; i < 3; i++)
				spawn([] (int *n) -> async::result<void> { ++*n; co_return; }(&n));
		}
	), [&] { done = true; });
	ASSERT_EQ(n, 3);
	ASSERT_TRUE(done);

	done = false;
	async::detach(async::dynamic_scope<async::result<void>>(frg::stl_allocator{},
		[] (auto &) { }
	), [&] { done = true; });
	ASSERT_TRUE(done);
}
