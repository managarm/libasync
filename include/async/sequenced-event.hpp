#pragma once

#include <async/recurring-event.hpp>
#include <async/cancellation.hpp>
#include <async/algorithm.hpp>
#include <assert.h>
#include <atomic>

namespace async {

struct sequenced_event {
	void raise() {
		seq_.fetch_add(1, std::memory_order_acq_rel);
		ev_.raise();
	}

	uint64_t next_sequence() {
		return seq_.load(std::memory_order_acquire) + 1;
	}

	auto async_wait(uint64_t in_seq, async::cancellation_token ct = {}) {
		return async::transform(ev_.async_wait_if([this, in_seq] {
			auto seq = seq_.load(std::memory_order_acquire);
			assert(seq >= in_seq);
			return in_seq >= seq;
		}, ct), [this] (auto) {
			return seq_.load(std::memory_order_acquire);
		});
	}

private:
	async::recurring_event ev_;
	std::atomic_uint64_t seq_ = 0;
};

} // namespace async
