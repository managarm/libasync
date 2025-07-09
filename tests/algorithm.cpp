#include <async/basic.hpp>
#include <async/result.hpp>
#include <async/algorithm.hpp>
#include <gtest/gtest.h>

TEST(Algorithm, Let) {
	int v = async::run([]() -> async::result<int> {
		co_return co_await async::let(
			[]() -> int { return 21; },
			[](int &ref) -> async::result<int> {
				co_return ref * 2;
			}
		);
	}());
	ASSERT_EQ(v, 42);
}

TEST(Algorithm, Sequence) {
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
	}());
	ASSERT_EQ(v, 4);

	for (int i = 0; i < 4; i++)
		ASSERT_EQ(steps[i], i);
}


struct dtor_counter {
	friend void swap(dtor_counter &a, dtor_counter &b) {
		std::swap(a.ctr_, b.ctr_);
	}

	dtor_counter() :ctr_{nullptr} { }
	dtor_counter(int &ctr) :ctr_{&ctr} { }

	~dtor_counter() {
		if (ctr_) (*ctr_)++;
	}

	dtor_counter(const dtor_counter &) = delete;
	dtor_counter(dtor_counter &&other) :dtor_counter{} {
		swap(*this, other);
	}

	dtor_counter &operator=(dtor_counter other) {
		swap(*this, other);
		return *this;
	}

	int *ctr_;
};

// These are defined as global so they are visible in lambdas without captures.

int g_lambda_ctr1;
int g_lambda_ctr2;

bool g_lambda_ok1;
bool g_lambda_ok2;

TEST(Algorithm, LambdaRaceAndCancel) {
	async::run(
		async::race_and_cancel(
			[x = dtor_counter{g_lambda_ctr1}] (async::cancellation_token ct)
			-> async::result<void> {
				if (g_lambda_ctr1 == 1) g_lambda_ok1 = true;
				co_return;
			},
			async::lambda(
				[x = dtor_counter{g_lambda_ctr2}] (async::cancellation_token ct)
				-> async::result<void> {
					if (g_lambda_ctr2 == 0) g_lambda_ok2 = true;
					co_return;
				}
			)
		));

	ASSERT_EQ(g_lambda_ctr1, 1);
	ASSERT_TRUE(g_lambda_ok1);

	ASSERT_EQ(g_lambda_ctr2, 1);
	ASSERT_TRUE(g_lambda_ok2);
}
