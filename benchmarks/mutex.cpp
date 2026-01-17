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
