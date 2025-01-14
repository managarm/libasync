#pragma once

#include <async/recurring-event.hpp>

namespace async {

struct barrier {
	using arrival_token = uint64_t;

	barrier(ptrdiff_t expected)
	: expected_{expected} { }

	arrival_token arrive(ptrdiff_t n = 1) {
		return do_arrive(n, 0);
	}

	arrival_token arrive_and_join(ptrdiff_t n = 1) {
		return do_arrive(n, n);
	}

	arrival_token arrive_and_drop(ptrdiff_t n = 1) {
		return do_arrive(0, -n);
	}

	auto async_wait(arrival_token s) {
		return evt_.async_wait_if([this, s] () -> bool {
			return seq_.load(std::memory_order_relaxed) == s;
		});
	}

private:
	arrival_token do_arrive(ptrdiff_t n, ptrdiff_t delta) {
		uint64_t s;
		bool advance = false;
		{
			frg::unique_lock lock{mutex_};

			s = seq_.load(std::memory_order_relaxed);
			assert(expected_ + delta >= 0);

			counter_ += n;
			expected_ += delta;

			if (counter_ == expected_) {
				advance = true;
				seq_.store(s + 1, std::memory_order_relaxed);
				counter_ = 0;
			} else {
				assert(counter_ < expected_);
			}
		}
		if (advance)
			evt_.raise();

		return s;
	}

	platform::mutex mutex_;
	// Sequence number. Increased after each barrier.
	// Write-protected by mutex_. Can be read even without holding mutex_.
	std::atomic<uint64_t> seq_{0};
	// Expected number of arrivals.
	// Protected by mutex_.
	ptrdiff_t expected_;
	// Arrival count. Reset to zero on each barrier.
	// Protected by mutex_.
	ptrdiff_t counter_{0};

	async::recurring_event evt_;
};

} // namespace async
