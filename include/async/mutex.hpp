#pragma once

#include <frg/list.hpp>
#include <async/basic.hpp>

namespace async {

namespace detail {
	struct mutex {
	private:
		struct node {
			node() = default;

			node(const node &) = delete;

			node &operator= (const node &) = delete;

			virtual void complete() = 0;

			frg::default_list_hook<node> hook;
		};

	public:
		mutex() = default;

		// ------------------------------------------------------------------------------
		// async_lock and boilerplate.
		// ------------------------------------------------------------------------------

		template<typename R>
		struct [[nodiscard]] lock_operation : private node {
			lock_operation(mutex *self, R receiver)
			: self_{self}, receiver_{std::move(receiver)} { }

			bool start_inline() {
				{
					frg::unique_lock lock(self_->mutex_);

					if(!self_->locked_) {
						// Fast path.
						self_->locked_ = true;
					}else{
						// Slow path.
						self_->waiters_.push_back(this);
						return false;
					}
				}

				execution::set_value_inline(receiver_);
				return true;
			}

		private:
			void complete() override {
				execution::set_value_noinline(receiver_);
			}

			mutex *self_;
			R receiver_;
		};

		struct [[nodiscard]] lock_sender {
			using value_type = void;

			template<typename R>
			lock_operation<R> connect(R receiver) {
				return {self, std::move(receiver)};
			}

			sender_awaiter<lock_sender>
			operator co_await () {
				return {*this};
			}

			mutex *self;
		};

		lock_sender async_lock() {
			return {this};
		}

		// ------------------------------------------------------------------------------

		bool try_lock() {
			frg::unique_lock lock(mutex_);

			if (locked_)
				return false;

			locked_ = true;
			return true;
		}

		void unlock() {
			node *next;
			{
				frg::unique_lock lock(mutex_);
				assert(locked_);

				if(waiters_.empty()) {
					locked_ = false;
					return;
				}

				next = waiters_.pop_front();
			}

			next->complete();
		}

	private:
		platform::mutex mutex_;

		bool locked_ = false;

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

using detail::mutex;

} // namespace async
