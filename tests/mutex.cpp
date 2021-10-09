#include <new>

#include <async/mutex.hpp>
#include <async/result.hpp>
#include <gtest/gtest.h>

TEST(Mutex, TryLock) {
	async::mutex m;

	async::run(m.async_lock());
	ASSERT_FALSE(m.try_lock());
	m.unlock();
	ASSERT_TRUE(m.try_lock());
	m.unlock();
}
