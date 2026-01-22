#pragma once

#include <async/algorithm.hpp>
#include <async/wait-group.hpp>
#include <async/cancellation.hpp>
#include <frg/functional.hpp>
#include <frg/list.hpp>

namespace async {

struct oneshot_event {
	void raise() {
		wg_.done();
	}

	auto wait(cancellation_token ct) {
		return wg_.wait(ct);
	}

	auto wait() {
		return wg_.wait();
	}
private:
	wait_group wg_ { 1 };
};

struct oneshot_primitive {
private:
	struct node {
		friend struct oneshot_primitive;

		node(void (*complete)(node *))
		: complete_{complete} { }

		node(const node &) = delete;

		node &operator= (const node &) = delete;

	protected:
		// Completion function.
		// This saves a few nanoseconds in benchmarks compared to a virtual function.
		void (*complete_)(node *);
		// Singly linked list of waiters.
		// Note that omitting this list does not improve benchmark performance.
		node *next_{nullptr};
	};

	// Sentinel value for state_. Cannot be a constexpr constant due to reinterpret_cast.
	static node *fired() {
		return reinterpret_cast<node *>(static_cast<uintptr_t>(1));
	}

public:
	void raise() {
		auto old = state_.exchange(fired(), std::memory_order_acq_rel);
		// Calling raise() twice is an error.
		assert(old != fired());
		while (old) {
			auto next = old->next_;
			old->complete_(old);
			old = next;
		}
	}

	// ----------------------------------------------------------------------------------
	// async_wait() and its boilerplate.
	// ----------------------------------------------------------------------------------

	template<typename Receiver>
	struct wait_operation final : private node {
		wait_operation(oneshot_primitive *evt, Receiver r)
		: node{&complete}, evt_{evt}, r_{std::move(r)} { }

		void start() {
			node *current = evt_->state_.load(std::memory_order_acquire);
			while (true) {
				if (current == fired())
					return execution::set_value(r_);
				next_ = current;
				auto success = evt_->state_.compare_exchange_weak(
					current,
					this,
					std::memory_order_acq_rel,
					std::memory_order_acquire
				);
				if (success)
					return;
			}
		}

	private:
		static void complete(node *base) {
			auto self = static_cast<wait_operation *>(base);
			execution::set_value_noinline(self->r_);
		}

		oneshot_primitive *evt_;
		Receiver r_;
	};

	struct wait_sender {
		using value_type = void;

		template<typename Receiver>
		friend wait_operation<Receiver> connect(wait_sender s, Receiver r) {
			return {s.evt, std::move(r)};
		}

		friend sender_awaiter<wait_sender, wait_sender::value_type> operator co_await (wait_sender s) {
			return {s};
		}

		oneshot_primitive *evt;
	};

	auto wait() {
		return wait_sender{this};
	}

private:
	// Possible states:
	// nullptr       => no waiter
	// valid pointer => waiter (i.e., head of list)
	// fired()       => event fired already
	std::atomic<node *> state_{nullptr};
};

} // namespace async
