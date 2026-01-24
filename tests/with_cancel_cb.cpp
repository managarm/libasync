#include <async/basic.hpp>
#include <async/result.hpp>
#include <async/cancellation.hpp>
#include <gtest/gtest.h>

TEST(Algorithm, WithCancelCbHappy) {
	bool cb_called = false;
	async::cancellation_event ce;

	int v = async::run(async::with_cancel_cb(
		[]() -> async::result<int> {
			co_return 42;
		}(),
		[&] {
			cb_called = true;
		},
		async::cancellation_token{ce}
	));

	ASSERT_EQ(v, 42);
	ASSERT_FALSE(cb_called);
}

TEST(Algorithm, WithCancelCbHappyVoid) {
	bool cb_called = false;
	async::cancellation_event ce;

	async::run(async::with_cancel_cb(
		[]() -> async::result<void> {
			co_return;
		}(),
		[&] {
			cb_called = true;
		},
		async::cancellation_token{ce}
	));

	ASSERT_FALSE(cb_called);
}

TEST(Algorithm, WithCancelCbCancelledBefore) {
	bool cb_called = false;
	async::cancellation_event ce;
	ce.cancel();

	int v = async::run(async::with_cancel_cb(
		[]() -> async::result<int> {
			co_return 42;
		}(),
		[&] {
			cb_called = true;
		},
		async::cancellation_token{ce}
	));

	ASSERT_EQ(v, 42);
	ASSERT_TRUE(cb_called);
}

TEST(Algorithm, WithCancelCbCancelledDuring) {
	bool cb_called = false;
	async::cancellation_event ce;
	ce.cancel();

	int v = async::run(async::with_cancel_cb(
		[](async::cancellation_event *evp) -> async::result<int> {
			evp->cancel();
			co_return 42;
		}(&ce),
		[&] {
			cb_called = true;
		},
		async::cancellation_token{ce}
	));

	ASSERT_EQ(v, 42);
	ASSERT_TRUE(cb_called);
}
