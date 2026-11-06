#include <async/cancellation.hpp>
#include <gtest/gtest.h>

TEST(DeferredCancellation, CallbacksRunOnInvoke) {
	async::cancellation_event ev;
	bool called = false;
	async::cancellation_callback cb{ev, [&] { called = true; }};

	auto deferred = ev.cancel_deferred();
	EXPECT_TRUE(async::cancellation_token{ev}.is_cancellation_requested());
	EXPECT_FALSE(called);

	deferred.invoke();
	EXPECT_TRUE(called);
}

TEST(DeferredCancellation, MultipleEvents) {
	async::cancellation_event ev1, ev2;
	bool called1 = false, called2 = false;
	async::cancellation_callback cb1{ev1, [&] { called1 = true; }};
	async::cancellation_callback cb2{ev2, [&] { called2 = true; }};

	async::deferred_cancellation deferred;
	ev1.cancel_deferred(deferred);
	ev2.cancel_deferred(deferred);
	EXPECT_FALSE(called1);
	EXPECT_FALSE(called2);

	deferred.invoke();
	EXPECT_TRUE(called1);
	EXPECT_TRUE(called2);
}

TEST(DeferredCancellation, LateCallbackRunsImmediately) {
	async::cancellation_event ev;
	auto deferred = ev.cancel_deferred();

	bool called = false;
	async::cancellation_callback cb{ev, [&] { called = true; }};
	EXPECT_TRUE(called);

	deferred.invoke();
}
