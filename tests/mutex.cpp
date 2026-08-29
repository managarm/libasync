#include <new>
#include <vector>

#include <async/basic.hpp>
#include <async/mutex.hpp>
#include <async/result.hpp>
#include <gtest/gtest.h>

namespace {

async::detached lockAndRecord(async::shared_mutex *mtx, std::vector<int> *order, int id) {
	co_await mtx->async_lock();
	order->push_back(id);
}

async::detached lockSharedAndRecord(async::shared_mutex *mtx, std::vector<int> *order, int id) {
	co_await mtx->async_lock_shared();
	order->push_back(id);
}

} // anonymous namespace

TEST(Mutex, TryLock) {
	async::mutex m;

	async::run(m.async_lock());
	ASSERT_FALSE(m.try_lock());
	m.unlock();
	ASSERT_TRUE(m.try_lock());
	m.unlock();
}

TEST(SharedMutex, Downgrade) {
	async::shared_mutex mtx;

	ASSERT_TRUE(mtx.try_lock());
	mtx.downgrade();

	// The mutex is now held in shared mode.
	ASSERT_FALSE(mtx.try_lock());
	ASSERT_TRUE(mtx.try_lock_shared());
	mtx.unlock_shared();

	mtx.unlock_shared();
	ASSERT_TRUE(mtx.try_lock());
	mtx.unlock();
}

TEST(SharedMutex, DowngradeWakesSharedWaiters) {
	async::shared_mutex mtx;
	std::vector<int> order;

	ASSERT_TRUE(mtx.try_lock());
	lockSharedAndRecord(&mtx, &order, 1);
	lockSharedAndRecord(&mtx, &order, 2);
	lockAndRecord(&mtx, &order, 3);
	ASSERT_TRUE(order.empty());

	// Both shared waiters can share the lock with us.
	mtx.downgrade();
	ASSERT_EQ(order, (std::vector<int>{1, 2}));

	// The exclusive waiter has to wait for all three shared owners.
	mtx.unlock_shared();
	mtx.unlock_shared();
	ASSERT_EQ(order, (std::vector<int>{1, 2}));
	mtx.unlock_shared();
	ASSERT_EQ(order, (std::vector<int>{1, 2, 3}));
	mtx.unlock();
}

TEST(SharedMutex, DowngradeBehindExclusiveWaiter) {
	async::shared_mutex mtx;
	std::vector<int> order;

	ASSERT_TRUE(mtx.try_lock());
	lockAndRecord(&mtx, &order, 1);
	lockSharedAndRecord(&mtx, &order, 2);

	// The shared waiter must not overtake the exclusive waiter,
	// hence downgrading does not wake any waiter here.
	mtx.downgrade();
	ASSERT_TRUE(order.empty());
	ASSERT_FALSE(mtx.try_lock_shared());

	mtx.unlock_shared();
	ASSERT_EQ(order, (std::vector<int>{1}));
	mtx.unlock();
	ASSERT_EQ(order, (std::vector<int>{1, 2}));
	mtx.unlock_shared();
}
