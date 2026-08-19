#include <atomic>
#include <thread>
#include <vector>

#include <async/algorithm.hpp>
#include <async/basic.hpp>
#include <async/oneshot-event.hpp>
#include <async/result.hpp>

#include <gtest/gtest.h>

namespace {

struct test_queue final : async::run_queue {
	int wakeups = 0;

	void wakeup() override {
		wakeups++;
	}
};

// Binds the default queue for the duration of a test so that the (thread-local)
// context is unbound again when the next test runs on the same thread.
struct default_queue_guard {
	default_queue_guard(async::run_queue *rq) {
		async::current_run_queue_context()->set_default_queue(rq);
	}

	default_queue_guard(const default_queue_guard &) = delete;

	~default_queue_guard() {
		async::current_run_queue_context()->reset_default_queue();
	}

	default_queue_guard &operator= (const default_queue_guard &) = delete;
};

struct test_io_service {
	async::run_queue *get_run_queue() {
		return rq;
	}

	void wait() {
		EXPECT_TRUE(rq->check());
		++*iterations;
		rq->run_iteration();
	}

	test_queue *rq;
	int *iterations;
};

struct test_item : async::run_queue_item {
	test_item(std::vector<int> *order, int id)
	: order{order}, id{id} {
		setup([] (async::run_queue_item *base) {
			auto self = static_cast<test_item *>(base);
			self->order->push_back(self->id);
		});
	}

	std::vector<int> *order;
	int id;
};

async::result<int> ready_value() {
	co_return 42;
}

async::result<void> await_ready_value(int *out) {
	*out = co_await ready_value();
}

} // namespace

TEST(RunQueue, SameThreadPostIsFifoAndNeedsNoWakeup) {
	test_queue rq;
	default_queue_guard dqg{&rq};

	std::vector<int> order;
	test_item a{&order, 1};
	test_item b{&order, 2};

	EXPECT_FALSE(rq.check());
	rq.post(&a);
	rq.post(&b);
	EXPECT_TRUE(rq.check());
	rq.run_iteration();
	EXPECT_FALSE(rq.check());
	EXPECT_EQ(order, (std::vector<int>{1, 2}));
	EXPECT_EQ(rq.wakeups, 0);
}

TEST(RunQueue, PostWhileDrainingRunsLifo) {
	test_queue rq;
	default_queue_guard dqg{&rq};

	std::vector<int> order;
	test_item x{&order, 2};
	test_item y{&order, 3};

	struct poster : async::run_queue_item {
		poster(test_queue *rq, std::vector<int> *order, test_item *x, test_item *y)
		: rq{rq}, order{order}, x{x}, y{y} {
			setup([] (async::run_queue_item *base) {
				auto self = static_cast<poster *>(base);
				self->order->push_back(1);
				self->rq->post(self->x);
				self->rq->post(self->y);
			});
		}

		test_queue *rq;
		std::vector<int> *order;
		test_item *x;
		test_item *y;
	};

	poster p{&rq, &order, &x, &y};
	rq.post(&p);
	rq.run_iteration();
	// Items posted while draining run in LIFO (call-stack) order.
	EXPECT_EQ(order, (std::vector<int>{1, 3, 2}));
}

TEST(RunQueue, CrossThreadPostInvokesWakeupOnce) {
	test_queue rq;
	default_queue_guard dqg{&rq};

	std::vector<int> order;
	test_item it{&order, 1};

	std::thread t{[&] {
		rq.post(&it);
	}};
	t.join();

	EXPECT_EQ(rq.wakeups, 1);
	EXPECT_TRUE(rq.check());
	rq.run_iteration();
	EXPECT_FALSE(rq.check());
	EXPECT_EQ(order, (std::vector<int>{1}));
}

TEST(RunQueue, CrossQueuePostNeedsRecheck) {
	test_queue rq1;
	test_queue rq2;

	std::vector<int> order;
	test_item b{&order, 2};

	struct poster : async::run_queue_item {
		poster(test_queue *rq, std::vector<int> *order, test_item *b)
		: rq{rq}, order{order}, b{b} {
			setup([] (async::run_queue_item *base) {
				auto self = static_cast<poster *>(base);
				self->order->push_back(1);
				self->rq->post(self->b);
			});
		}

		test_queue *rq;
		std::vector<int> *order;
		test_item *b;
	};

	poster p{&rq2, &order, &b};
	rq1.post(&p);
	rq1.run_iteration();

	// Draining rq1 queued work on rq2 without a wakeup: runtimes must re-check
	// all of their queues before they block.
	EXPECT_FALSE(rq1.check());
	EXPECT_TRUE(rq2.check());
	EXPECT_EQ(rq2.wakeups, 0);

	rq2.run_iteration();
	EXPECT_EQ(order, (std::vector<int>{1, 2}));
}

TEST(RunQueue, ReadySenderStillResumesFromLoop) {
	test_queue rq;
	default_queue_guard dqg{&rq};

	int iterations = 0;
	int out = 0;
	async::run(await_ready_value(&out), test_io_service{&rq, &iterations});
	EXPECT_EQ(out, 42);
	// Even a synchronously completing sender resumes the coroutine from the loop.
	EXPECT_EQ(iterations, 1);
}

TEST(RunQueue, EventRaiseDefersResumeToQueue) {
	test_queue rq;
	default_queue_guard dqg{&rq};

	async::oneshot_event ev;
	bool done = false;

	auto waiter = [] (async::oneshot_event &ev, bool *done) -> async::result<void> {
		co_await ev.wait();
		*done = true;
	};
	async::detach(waiter(ev, &done));

	ev.raise();
	// The waiter must be resumed by the run queue, not from raise().
	EXPECT_FALSE(done);
	EXPECT_TRUE(rq.check());
	rq.run_iteration();
	EXPECT_TRUE(done);
}

TEST(RunQueue, WhenAllPropagatesRunQueueToChildren) {
	test_queue rq;
	default_queue_guard dqg{&rq};

	async::oneshot_event ev;
	bool observed = false;
	bool raised = false;

	auto waiter = [] (async::oneshot_event &ev, bool *observed) -> async::result<void> {
		co_await ev.wait();
		*observed = true;
	};
	auto raiser = [] (async::oneshot_event &ev, bool *observed, bool *raised) -> async::result<void> {
		co_await ready_value();
		ev.raise();
		// The waiter was posted to the run queue; it must not have run inline.
		EXPECT_FALSE(*observed);
		*raised = true;
	};

	int iterations = 0;
	async::run(async::when_all(waiter(ev, &observed), raiser(ev, &observed, &raised)),
			test_io_service{&rq, &iterations});
	EXPECT_TRUE(observed);
	EXPECT_TRUE(raised);
}

TEST(RunQueue, NestedRunIterationDrainsForOuter) {
	test_queue rq;
	default_queue_guard dqg{&rq};

	int iterations = 0;
	int nestedIterations = 0;
	int out = 0;
	auto outer = [&] () -> async::result<void> {
		co_await ready_value();
		// Drive a nested event loop from within the outer loop's run_iteration().
		async::run(await_ready_value(&out), test_io_service{&rq, &nestedIterations});
	};
	async::run(outer(), test_io_service{&rq, &iterations});
	EXPECT_EQ(out, 42);
	EXPECT_EQ(nestedIterations, 1);
}

TEST(RunQueue, UnboundThreadResumesInline) {
	// No queue is bound: awaits must resume inline and never touch a loop
	// (dummy_io_service panics if wait() is called).
	int out = 0;
	async::run(await_ready_value(&out));
	EXPECT_EQ(out, 42);
}
