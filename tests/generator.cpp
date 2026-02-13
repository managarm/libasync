#include <async/generator.hpp>
#include <async/basic.hpp>
#include <async/result.hpp>
#include <gtest/gtest.h>
#include <memory>

namespace {

async::generator<int> generate_nothing() {
	co_return;
}

async::generator<int> generate_ints() {
	co_yield 1;
	co_yield 2;
	co_yield 3;
}

async::generator<std::unique_ptr<int>> generate_unique_ptrs() {
	co_yield std::make_unique<int>(1);
	co_yield std::make_unique<int>(2);
	co_yield std::make_unique<int>(3);
}

} // anonymous namespace

TEST(Generator, YieldNothing) {
	async::run([]() -> async::result<void> {
		auto gen = generate_nothing();

		auto v = co_await gen.next();
		EXPECT_FALSE(v.has_value());
	}());
}

TEST(Generator, YieldInts) {
	async::run([]() -> async::result<void> {
		auto gen = generate_ints();

		auto v1 = co_await gen.next();
		EXPECT_TRUE(v1.has_value());
		EXPECT_EQ(*v1, 1);

		auto v2 = co_await gen.next();
		EXPECT_TRUE(v2.has_value());
		EXPECT_EQ(*v2, 2);

		auto v3 = co_await gen.next();
		EXPECT_TRUE(v3.has_value());
		EXPECT_EQ(*v3, 3);

		auto v4 = co_await gen.next();
		EXPECT_FALSE(v4.has_value());
	}());
}

TEST(Generator, YieldMoveOnly) {
	async::run([]() -> async::result<void> {
		auto gen = generate_unique_ptrs();

		auto v1 = co_await gen.next();
		EXPECT_TRUE(v1.has_value());
		EXPECT_EQ(**v1, 1);

		auto v2 = co_await gen.next();
		EXPECT_TRUE(v2.has_value());
		EXPECT_EQ(**v2, 2);

		auto v3 = co_await gen.next();
		EXPECT_TRUE(v3.has_value());
		EXPECT_EQ(**v3, 3);

		auto v4 = co_await gen.next();
		EXPECT_FALSE(v4.has_value());
	}());
}
