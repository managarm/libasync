#include <benchmark/benchmark.h>
#include <async/oneshot-event.hpp>
#include <async/basic.hpp>

static void BM_RaiseWait_OneshotEvent(benchmark::State& state) {
	for (auto _ : state) {
		async::oneshot_event ev;
		ev.raise();
		async::run(ev.wait());
	}
}
BENCHMARK(BM_RaiseWait_OneshotEvent);

static void BM_RaiseWait_OneshotPrimitive(benchmark::State& state) {
	for (auto _ : state) {
		async::oneshot_primitive ev;
		ev.raise();
		async::run(ev.wait());
	}
}
BENCHMARK(BM_RaiseWait_OneshotPrimitive);

static void BM_WaitRaise_OneshotEvent(benchmark::State& state) {
	for (auto _ : state) {
		async::oneshot_event ev;
		bool done = false;
		auto coro = [] (async::oneshot_event *ev_p, bool *done_p) -> async::detached {
			co_await ev_p->wait();
			*done_p = true;
		};
		coro(&ev, &done);
		ev.raise();
		benchmark::DoNotOptimize(done);
	}
}
BENCHMARK(BM_WaitRaise_OneshotEvent);

static void BM_WaitRaise_OneshotPrimitive(benchmark::State& state) {
	for (auto _ : state) {
		async::oneshot_primitive ev;
		bool done = false;
		auto coro = [] (async::oneshot_primitive *ev_p, bool *done_p) -> async::detached {
			co_await ev_p->wait();
			*done_p = true;
		};
		coro(&ev, &done);
		ev.raise();
		benchmark::DoNotOptimize(done);
	}
}
BENCHMARK(BM_WaitRaise_OneshotPrimitive);

static void BM_WaitTwiceRaise_OneshotEvent(benchmark::State& state) {
	for (auto _ : state) {
		async::oneshot_event ev;
		int done = 0;
		auto coro = [] (async::oneshot_event *ev_p, int *done_p) -> async::detached {
			co_await ev_p->wait();
			(*done_p)++;
		};
		coro(&ev, &done);
		coro(&ev, &done);
		ev.raise();
		assert(done == 2);
		benchmark::DoNotOptimize(done);
	}
}
BENCHMARK(BM_WaitTwiceRaise_OneshotEvent);

static void BM_WaitTwiceRaise_OneshotPrimitive(benchmark::State& state) {
	for (auto _ : state) {
		async::oneshot_primitive ev;
		int done = 0;
		auto coro = [] (async::oneshot_primitive *ev_p, int *done_p) -> async::detached {
			co_await ev_p->wait();
			(*done_p)++;
		};
		coro(&ev, &done);
		coro(&ev, &done);
		ev.raise();
		assert(done == 2);
		benchmark::DoNotOptimize(done);
	}
}
BENCHMARK(BM_WaitTwiceRaise_OneshotPrimitive);
