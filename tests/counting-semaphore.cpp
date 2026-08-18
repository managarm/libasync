#include <vector>

#include <async/basic.hpp>
#include <async/counting-semaphore.hpp>
#include <gtest/gtest.h>

namespace {

async::detached acquireAndRecord(async::counting_semaphore *sem, size_t n,
		std::vector<int> *order, int id) {
	co_await sem->async_acquire(n);
	order->push_back(id);
}

} // anonymous namespace

TEST(CountingSemaphore, TryAcquire) {
	async::counting_semaphore sem{2};

	ASSERT_FALSE(sem.try_acquire(3));
	ASSERT_TRUE(sem.try_acquire(2));
	ASSERT_FALSE(sem.try_acquire(1));
	sem.release(1);
	ASSERT_TRUE(sem.try_acquire(1));
}

TEST(CountingSemaphore, AcquireFastPath) {
	async::counting_semaphore sem{3};
	std::vector<int> order;

	acquireAndRecord(&sem, 2, &order, 1);
	ASSERT_EQ(order.size(), 1u);
	acquireAndRecord(&sem, 1, &order, 2);
	ASSERT_EQ(order.size(), 2u);
	acquireAndRecord(&sem, 1, &order, 3);
	ASSERT_EQ(order.size(), 2u);
	sem.release(1);
	ASSERT_EQ(order.size(), 3u);
}

TEST(CountingSemaphore, FifoOrder) {
	async::counting_semaphore sem{0};
	std::vector<int> order;

	acquireAndRecord(&sem, 1, &order, 1);
	acquireAndRecord(&sem, 1, &order, 2);
	acquireAndRecord(&sem, 1, &order, 3);
	ASSERT_TRUE(order.empty());

	sem.release(1);
	ASSERT_EQ(order, (std::vector<int>{1}));
	sem.release(2);
	ASSERT_EQ(order, (std::vector<int>{1, 2, 3}));
}

TEST(CountingSemaphore, NoOvertaking) {
	async::counting_semaphore sem{0};
	std::vector<int> order;

	// The front waiter needs more units than the second one.
	acquireAndRecord(&sem, 3, &order, 1);
	acquireAndRecord(&sem, 1, &order, 2);

	// The second waiter must not overtake the front waiter,
	// even though enough units are available for it.
	sem.release(2);
	ASSERT_TRUE(order.empty());
	ASSERT_FALSE(sem.try_acquire(1));

	sem.release(2);
	ASSERT_EQ(order, (std::vector<int>{1, 2}));
	ASSERT_FALSE(sem.try_acquire(1));
}

TEST(CountingSemaphore, ReleaseSatisfiesMultipleWaiters) {
	async::counting_semaphore sem{0};
	std::vector<int> order;

	acquireAndRecord(&sem, 2, &order, 1);
	acquireAndRecord(&sem, 3, &order, 2);
	acquireAndRecord(&sem, 1, &order, 3);

	sem.release(6);
	ASSERT_EQ(order, (std::vector<int>{1, 2, 3}));
	ASSERT_FALSE(sem.try_acquire(1));
}
