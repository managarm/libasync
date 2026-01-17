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

		protected:
			~node() = default;
		};

	public:
		mutex() = default;

		// ------------------------------------------------------------------------------
		// async_lock and boilerplate.
		// ------------------------------------------------------------------------------

		template<typename R>
		struct [[nodiscard]] lock_operation final : private node {
			lock_operation(mutex *self, R receiver)
			: self_{self}, receiver_{std::move(receiver)} { }

			bool start_inline() {
				// Avoid taking mutex_ if possible.
				if (self_->try_lock()) {
					execution::set_value_inline(receiver_);
					return true;
				}

				{
					frg::unique_lock lock(self_->mutex_);

					auto st = self_->st_.load(std::memory_order_relaxed);
					while (true) {
						if (st == state::none) {
							bool success = self_->st_.compare_exchange_weak(
								st,
								state::locked,
								std::memory_order_acquire,
								std::memory_order_relaxed
							);
							if (success)
								break;
						} else if (st == state::locked) {
							// CAS since there can be concurrent transitions from state::locked.
							bool success = self_->st_.compare_exchange_weak(
								st,
								state::contended,
								std::memory_order_relaxed,
								std::memory_order_relaxed
							);
							if (success) {
								self_->waiters_.push_back(this);
								return false;
							}
						} else {
							// mutex_ protects against concurrent transitions from state::contended.
							assert(st == state::contended);
							self_->waiters_.push_back(this);
							return false;
						}
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
			auto st = state::none;
			return st_.compare_exchange_strong(
				st,
				state::locked,
				std::memory_order_acquire,
				std::memory_order_relaxed
			);
		}

		void unlock() {
			auto st = st_.load(std::memory_order_relaxed);
			assert(st != state::none);

			// If there is no contention, we can unlock without taking mutex_.
			if (st == state::locked) {
				bool success = st_.compare_exchange_strong(
					st,
					state::none,
					std::memory_order_release,
					std::memory_order_relaxed
				);
				if (success)
					return;
			}
			// Only the owner ever transitions out of state::locked so we must be in state::contended.
			assert(st == state::contended);

			node *next;
			{
				frg::unique_lock lock(mutex_);

				// Otherwise, we would not be in state::contended.
				assert(!waiters_.empty());

				next = waiters_.pop_front();
				if (waiters_.empty()) {
					// Hand-off to a waiter does not require a fence.
					st_.store(state::locked, std::memory_order_relaxed);
				}
			}

			next->complete();
		}

	private:
		enum class state {
			none,
			locked,
			contended,
		};

		platform::mutex mutex_;

		// State transitions are protected by mutex_ except for the transitions:
		// * state::none -> state::locked
		// * state::locked -> state::none
		// which can happen outside of mutex_.
		std::atomic<state> st_{state::none};

		frg::intrusive_list<
			node,
			frg::locate_member<
				node,
				frg::default_list_hook<node>,
				&node::hook
			>
		> waiters_;
	};

	struct shared_mutex {
	private:
		enum class state {
			none,
			shared,
			exclusive
		};

		struct node {
			node() = default;

			node(const node &) = delete;

			node &operator= (const node &) = delete;

		protected:
			~node() = default;

		public:
			virtual void complete() = 0;

			frg::default_list_hook<node> hook;
			state desired;
		};

	public:
		shared_mutex() = default;

		// ------------------------------------------------------------------------------
		// async_lock and boilerplate.
		// ------------------------------------------------------------------------------

		template<typename R>
		struct [[nodiscard]] lock_operation final : private node {
		private:
			using node::desired;

		public:
			lock_operation(shared_mutex *self, R receiver)
			: self_{self}, receiver_{std::move(receiver)} {
				desired = state::exclusive;
			}

			bool start_inline() {
				{
					frg::unique_lock lock(self_->mutex_);

					if(self_->st_ != state::none) {
						// Slow path.
						self_->waiters_.push_back(this);
						return false;
					}

					// Fast path.
					self_->st_ = state::exclusive;
				}

				execution::set_value_inline(receiver_);
				return true;
			}

		private:
			void complete() override {
				execution::set_value_noinline(receiver_);
			}

			shared_mutex *self_;
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

			shared_mutex *self;
		};

		lock_sender async_lock() {
			return {this};
		}

		// ------------------------------------------------------------------------------
		// async_lock_shared and boilerplate.
		// ------------------------------------------------------------------------------

		template<typename R>
		struct [[nodiscard]] lock_shared_operation final : private node {
		private:
			using node::desired;

		public:
			lock_shared_operation(shared_mutex *self, R receiver)
			: self_{self}, receiver_{std::move(receiver)} {
				desired = state::shared;
			}

			bool start_inline() {
				{
					frg::unique_lock lock(self_->mutex_);

					if(self_->st_ == state::none) {
						// Fast path.
						assert(!self_->shared_cnt_);
						self_->st_ = state::shared;
						self_->shared_cnt_ = 1;
					}else if(self_->st_ == state::shared && self_->waiters_.empty()) {
						// Fast path.
						assert(self_->shared_cnt_);
						++self_->shared_cnt_;
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

			shared_mutex *self_;
			R receiver_;
		};

		struct [[nodiscard]] lock_shared_sender {
			using value_type = void;

			template<typename R>
			lock_shared_operation<R> connect(R receiver) {
				return {self, std::move(receiver)};
			}

			sender_awaiter<lock_shared_sender>
			operator co_await () {
				return {*this};
			}

			shared_mutex *self;
		};

		lock_shared_sender async_lock_shared() {
			return {this};
		}

		// ------------------------------------------------------------------------------

		void unlock() {
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
				assert(st_ == state::exclusive);

				if(waiters_.empty()) {
					st_ = state::none;
					return;
				}

				if(waiters_.front()->desired == state::exclusive) {
					pending.push_back(waiters_.pop_front());
				}else{
					assert(!shared_cnt_);
					st_ = state::shared;
					while(!waiters_.empty() && waiters_.front()->desired == state::shared) {
						pending.push_back(waiters_.pop_front());
						++shared_cnt_;
					}
				}
			}
			assert(!pending.empty());

			while(!pending.empty())
				pending.pop_front()->complete();
		}

		void unlock_shared() {
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
				assert(st_ == state::shared);
				assert(shared_cnt_);

				--shared_cnt_;
				if(shared_cnt_)
					return;

				if(waiters_.empty()) {
					st_ = state::none;
					return;
				}

				assert(waiters_.front()->desired == state::exclusive);
				st_ = state::exclusive;
				pending.push_back(waiters_.pop_front());
			}
			assert(!pending.empty());

			while(!pending.empty())
				pending.pop_front()->complete();
		}

	private:
		platform::mutex mutex_;

		state st_ = state::none;

		unsigned int shared_cnt_ = 0;

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
using detail::shared_mutex;

} // namespace async
