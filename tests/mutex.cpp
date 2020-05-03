#include <new>

#include <async/mutex.hpp>
#include <async/result.hpp>
#include <gtest/gtest.h>

TEST(Mutex, TryLock) {
	async::run_queue rq;
	async::queue_scope qs{&rq};

	async::mutex m;

	async::run(m.async_lock(), async::current_queue);
	ASSERT_FALSE(m.try_lock());
	m.unlock();
	ASSERT_TRUE(m.try_lock());
	m.unlock();
}
