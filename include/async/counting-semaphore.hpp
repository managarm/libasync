#pragma once

#include <cstddef>
#include <cstdint>

#include <frg/list.hpp>
#include <async/basic.hpp>

namespace async {

namespace detail {
	// Fair counting semaphore: waiters are served strictly in FIFO order and
	// acquire all of their requested units at once (i.e., all-or-nothing).
	struct counting_semaphore {
	private:
		struct node {
			node() = default;

			node(const node &) = delete;

			node &operator= (const node &) = delete;

			virtual void complete() = 0;

			frg::default_list_hook<node> hook;
			// Number of units that this waiter wants to acquire.
			size_t needed = 0;

		protected:
			~node() = default;
		};

	public:
		counting_semaphore(size_t count = 0)
		: count_{count} { }

		// ------------------------------------------------------------------------------
		// async_acquire and boilerplate.
		// ------------------------------------------------------------------------------

		template<typename R>
		struct [[nodiscard]] acquire_operation final : private node {
			acquire_operation(counting_semaphore *self, size_t n, R receiver)
			: self_{self}, receiver_{std::move(receiver)} {
				needed = n;
			}

			void start() {
				{
					frg::unique_lock lock(self_->mutex_);

					// To ensure fairness, we also wait if there are earlier waiters,
					// even if enough units are available for this operation.
					if(!self_->waiters_.empty() || self_->count_ < needed) {
						self_->waiters_.push_back(this);
						return;
					}
					self_->count_ -= needed;
				}

				return execution::set_value(receiver_);
			}

		private:
			void complete() override {
				execution::set_value(receiver_);
			}

			counting_semaphore *self_;
			R receiver_;
		};

		struct [[nodiscard]] acquire_sender {
			using value_type = void;

			template<typename R>
			acquire_operation<R> connect(R receiver) {
				return {self, n, std::move(receiver)};
			}

			sender_awaiter<acquire_sender>
			operator co_await () {
				return {*this};
			}

			counting_semaphore *self;
			size_t n;
		};

		acquire_sender async_acquire(size_t n = 1) {
			return {this, n};
		}

		// ------------------------------------------------------------------------------

		bool try_acquire(size_t n = 1) {
			frg::unique_lock lock(mutex_);

			// To ensure fairness, we also fail if there are earlier waiters,
			// even if enough units are available.
			if(!waiters_.empty() || count_ < n)
				return false;
			count_ -= n;
			return true;
		}

		void release(size_t n = 1) {
			frg::intrusive_list<
				node,
				frg::locate_member<
					node,
					frg::default_list_hook<node>,
					&node::hook
				>
			> pending;
			{
				frg::unique_lock lock(mutex_);

				assert(count_ <= SIZE_MAX - n);
				count_ += n;
				// Satisfy waiters in FIFO order; if the front waiter needs more
				// units than are available, later waiters must not overtake it.
				while(!waiters_.empty() && waiters_.front()->needed <= count_) {
					count_ -= waiters_.front()->needed;
					pending.push_back(waiters_.pop_front());
				}
			}

			while(!pending.empty())
				pending.pop_front()->complete();
		}

	private:
		platform::mutex mutex_;

		// Number of available units; protected by mutex_.
		size_t count_;

		frg::intrusive_list<
			node,
			frg::locate_member<
				node,
				frg::default_list_hook<node>,
				&node::hook
			>
		> waiters_;
	};
}

using detail::counting_semaphore;

} // namespace async
