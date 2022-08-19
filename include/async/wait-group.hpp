#pragma once

/* refactored from oneshot_event */

#include <atomic>
#include <async/algorithm.hpp>
#include <async/cancellation.hpp>
#include <frg/functional.hpp>
#include <frg/list.hpp>

namespace async {

struct wait_group {
private:
	struct node {
		friend struct wait_group;

		node() = default;

		node(const node &) = delete;

		node &operator= (const node &) = delete;

		virtual void complete() = 0;

	protected:
		virtual ~node() = default;

	private:
		// Protected by mutex_.
		frg::default_list_hook<node> _hook;
	};

public:
	wait_group(size_t ctr) : ctr_ { ctr } {}

	void done() {
		// Grab all items and mark them as retired while we hold the lock.
		frg::intrusive_list<
			node,
			frg::locate_member<
				node,
				frg::default_list_hook<node>,
				&node::_hook
			>
		> items;

		while (1) {
			auto v = ctr_.load(std::memory_order_acquire);
			if (ctr_.compare_exchange_strong(v, v - 1, std::memory_order_acq_rel, std::memory_order_acquire)) {
				assert(v > 0);
				if (v != 1) {
					return;
				} else {
					break;
				}
			}
		}

		{
			frg::unique_lock lock(mutex_);
			items.splice(items.end(), queue_);
		}

		// Now invoke the individual callbacks.
		while(!items.empty()) {
			auto item = items.front();
			items.pop_front();
			item->complete();
		}
	}

	void add(size_t new_members) {
		ctr_.fetch_add(new_members, std::memory_order_acq_rel);
	}

	// ----------------------------------------------------------------------------------
	// wait() and its boilerplate.
	// ----------------------------------------------------------------------------------

	template<typename Receiver>
	struct wait_operation final : private node {
		wait_operation(wait_group *wg, cancellation_token ct, Receiver r)
		: wg_{wg}, ct_{std::move(ct)}, r_{std::move(r)}, cobs_{this} { }

		bool start_inline() {
			bool cancelled = false;
			{
				frg::unique_lock lock(wg_->mutex_);

				if(wg_->ctr_.load(std::memory_order_acquire) > 0) {
					if(!cobs_.try_set(ct_)) {
						cancelled = true;
					}else{
						wg_->queue_.push_back(this);
						return false;
					}
				}
			}

			execution::set_value_inline(r_, !cancelled);
			return true;
		}

	private:
		void cancel() {
			bool cancelled = false;
			{
				frg::unique_lock lock(wg_->mutex_);

				if(wg_->ctr_.load(std::memory_order_acquire) > 0) {
					cancelled = true;
					auto it = wg_->queue_.iterator_to(this);
					wg_->queue_.erase(it);
				}
			}

			execution::set_value_noinline(r_, !cancelled);
		}

		void complete() override {
			if(cobs_.try_reset())
				execution::set_value_noinline(r_, true);
		}

		wait_group *wg_;
		cancellation_token ct_;
		Receiver r_;
		cancellation_observer<frg::bound_mem_fn<&wait_operation::cancel>> cobs_;
	};

	struct [[nodiscard]] wait_sender {
		using value_type = bool;

		template<typename Receiver>
		friend wait_operation<Receiver> connect(wait_sender s, Receiver r) {
			return {s.wg, s.ct, std::move(r)};
		}

		sender_awaiter<wait_sender, bool> operator co_await () {
			return {*this};
		}

		wait_group *wg;
		cancellation_token ct;
	};

	wait_sender wait(cancellation_token ct) {
		return {this, ct};
	}

	auto wait() {
		return async::transform(wait(cancellation_token{}), [] (bool waitSuccess) {
			assert(waitSuccess);
		});
	}

	/* BasicLockable support */
	void lock() { add(1); }
	void unlock() { done(); }
private:
	platform::mutex mutex_;

	std::atomic<size_t> ctr_;

	frg::intrusive_list<
		node,
		frg::locate_member<
			node,
			frg::default_list_hook<node>,
			&node::_hook
		>
	> queue_;
};

} // namespace async
