#include <benchmark/benchmark.h>
#include <async/mutex.hpp>

static void BM_TryLock_Mutex(benchmark::State& state) {
	async::mutex m;
	for (auto _ : state) {
		auto success = m.try_lock();
		assert(success);
		m.unlock();
	}
}
BENCHMARK(BM_TryLock_Mutex);

static void BM_TryLock_SharedMutex(benchmark::State& state) {
	async::shared_mutex m;
	for (auto _ : state) {
		auto success = m.try_lock();
		assert(success);
		m.unlock();
	}
}
BENCHMARK(BM_TryLock_SharedMutex);

static void BM_TryLockShared_SharedMutex(benchmark::State& state) {
	async::shared_mutex m;
	for (auto _ : state) {
		auto success = m.try_lock_shared();
		assert(success);
		m.unlock_shared();
	}
}
BENCHMARK(BM_TryLockShared_SharedMutex);
