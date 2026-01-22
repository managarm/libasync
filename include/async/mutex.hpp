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

			void start() {
				// Avoid taking mutex_ if possible.
				if (self_->try_lock())
					return execution::set_value(receiver_);

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
								return;
							}
						} else {
							// mutex_ protects against concurrent transitions from state::contended.
							assert(st == state::contended);
							self_->waiters_.push_back(this);
							return;
						}
					}
				}

				return execution::set_value(receiver_);
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
		enum class contention : int {
			none,
			locked,
			contended
		};

		// Shared locks are represented by shared_cnt > 0.
		struct alignas(8) state {
			contention c;
			unsigned int shared_cnt;
		};
		static_assert(std::atomic<state>::is_always_lock_free);


		struct node {
			node() = default;

			node(const node &) = delete;

			node &operator= (const node &) = delete;

		protected:
			~node() = default;

		public:
			virtual void complete() = 0;

			frg::default_list_hook<node> hook;
			bool exclusive;
		};

	public:
		shared_mutex() = default;

		// ------------------------------------------------------------------------------
		// async_lock and boilerplate.
		// ------------------------------------------------------------------------------

		template<typename R>
		struct [[nodiscard]] lock_operation final : private node {
		private:
			using node::exclusive;

		public:
			lock_operation(shared_mutex *self, R receiver)
			: self_{self}, receiver_{std::move(receiver)} {
				exclusive = true;
			}

			void start() {
				if (self_->try_lock())
					return execution::set_value(receiver_);

				{
					frg::unique_lock lock(self_->mutex_);

					auto st = self_->st_.load(std::memory_order_relaxed);
					while (true) {
						if (st.c == contention::none) {
							bool success = self_->st_.compare_exchange_weak(
								st,
								state{.c = contention::locked, .shared_cnt = 0},
								std::memory_order_acquire,
								std::memory_order_relaxed
							);
							if (success)
								break;
						} else if (st.c == contention::locked) {
							// CAS since there can be concurrent transitions from contention::locked.
							bool success = self_->st_.compare_exchange_weak(
								st,
								state{.c = contention::contended, .shared_cnt = st.shared_cnt},
								std::memory_order_relaxed,
								std::memory_order_relaxed
							);
							if (success) {
								self_->waiters_.push_back(this);
								return;
							}
						} else {
							// mutex_ protects against concurrent transitions from contention::contended.
							assert(st.c == contention::contended);
							self_->waiters_.push_back(this);
							return;
						}
					}
				}

				return execution::set_value(receiver_);
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
			using node::exclusive;

		public:
			lock_shared_operation(shared_mutex *self, R receiver)
			: self_{self}, receiver_{std::move(receiver)} {
				exclusive = false;
			}

			void start() {
				if (self_->try_lock_shared())
					return execution::set_value(receiver_);

				{
					frg::unique_lock lock(self_->mutex_);

					auto st = self_->st_.load(std::memory_order_relaxed);
					while (true) {
						if (st.c == contention::none) {
							bool success = self_->st_.compare_exchange_weak(
								st,
								state{.c = contention::locked, .shared_cnt = 1},
								std::memory_order_acquire,
								std::memory_order_relaxed
							);
							if (success)
								break;
						} else if (st.c == contention::locked && st.shared_cnt) {
							// CAS since there can be concurrent transitions from contention::locked.
							bool success = self_->st_.compare_exchange_weak(
								st,
								state{.c = contention::locked, .shared_cnt = st.shared_cnt + 1},
								std::memory_order_acquire,
								std::memory_order_relaxed
							);
							if (success)
								break;
						} else {
							if (st.c == contention::locked) {
								assert(!st.shared_cnt);
								// CAS since there can be concurrent transitions from contention::locked.
								bool success = self_->st_.compare_exchange_weak(
									st,
									state{.c = contention::contended, .shared_cnt = 0},
									std::memory_order_relaxed,
									std::memory_order_relaxed
								);
								if (success) {
									self_->waiters_.push_back(this);
									return;
								}
							} else {
								// mutex_ protects against concurrent transitions from contention::contended.
								assert(st.c == contention::contended);
								self_->waiters_.push_back(this);
								self_->st_.store(
									state{.c = contention::contended, .shared_cnt = st.shared_cnt},
									std::memory_order_relaxed
								);
								return;
							}
						}
					}
				}

				return execution::set_value(receiver_);
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

		bool try_lock() {
			auto st = state{.c = contention::none, .shared_cnt = 0};
			return st_.compare_exchange_strong(
				st,
				state{.c = contention::locked, .shared_cnt = 0},
				std::memory_order_acquire,
				std::memory_order_relaxed
			);
		}

		bool try_lock_shared() {
			auto st = st_.load(std::memory_order_relaxed);
			while (true) {
				if (st.c == contention::none) {
					bool success = st_.compare_exchange_strong(
						st,
						state{.c = contention::locked, .shared_cnt = 1},
						std::memory_order_acquire,
						std::memory_order_relaxed
					);
					if (success)
						return true;
				} else if(st.c == contention::locked && st.shared_cnt) {
					bool success = st_.compare_exchange_strong(
						st,
						state{.c = contention::locked, .shared_cnt = st.shared_cnt + 1},
						std::memory_order_acquire,
						std::memory_order_relaxed
					);
					if (success)
						return true;
				} else {
					return false;
				}
			}
		}

		void unlock() {
			auto st = st_.load(std::memory_order_relaxed);
			assert(st.c != contention::none);
			assert(!st.shared_cnt);

			// If there is no contention, we can unlock without taking mutex_.
			if (st.c == contention::locked) {
				bool success = st_.compare_exchange_strong(
					st,
					state{.c = contention::none, .shared_cnt = 0},
					std::memory_order_release,
					std::memory_order_relaxed
				);
				if (success)
					return;
				assert(!st.shared_cnt);
			}
			// Only the owner ever transitions out of state::locked so we must be in state::contended.
			assert(st.c == contention::contended);

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

				// Otherwise, we would not be in state::contended.
				assert(!waiters_.empty());

				if(waiters_.front()->exclusive) {
					pending.push_back(waiters_.pop_front());
					// Hand-off to a waiter does not require a fence.
					if (waiters_.empty()) {
						st_.store(
							state{.c = contention::locked, .shared_cnt = 0},
							std::memory_order_relaxed
						);
					}
				}else{
					unsigned int n = 0;
					while(!waiters_.empty() && !waiters_.front()->exclusive) {
						pending.push_back(waiters_.pop_front());
						++n;
					}
					// Hand-off to a waiter does not require a fence.
					if (waiters_.empty()) {
						st_.store(
							state{.c = contention::locked, .shared_cnt = n},
							std::memory_order_relaxed
						);
					} else {
						st_.store(
							state{.c = contention::contended, .shared_cnt = n},
							std::memory_order_relaxed
						);
					}
				}
			}
			assert(!pending.empty());

			while(!pending.empty())
				pending.pop_front()->complete();
		}

		void unlock_shared() {
			auto st = st_.load(std::memory_order_relaxed);
			assert(st.c != contention::none);
			assert(st.shared_cnt);

			// If there is no contention, we can unlock without taking mutex_.
			// In contrast to unlock(), we need to loop here.
			while (st.c == contention::locked) {
				if (st.shared_cnt > 1) {
					bool success = st_.compare_exchange_weak(
						st,
						state{.c = contention::locked, .shared_cnt = st.shared_cnt - 1},
						std::memory_order_release,
						std::memory_order_relaxed
					);
					if (success)
						return;
				} else {
					assert(st.shared_cnt == 1);
					bool success = st_.compare_exchange_weak(
						st,
						state{.c = contention::none, .shared_cnt = 0},
						std::memory_order_release,
						std::memory_order_relaxed
					);
					if (success)
						return;
				}
				assert(st.c != contention::none);
				assert(st.shared_cnt);
			}
			// Only the owner ever transitions out of state::locked so we must be in state::contended.
			assert(st.c == contention::contended);

			node *next;
			{
				frg::unique_lock lock(mutex_);

				// In contrast to unlock(), contended shared -> shared transitions do not wake.
				if (st.shared_cnt > 1) {
					st_.store(
						state{.c = contention::contended, .shared_cnt = st.shared_cnt - 1},
						std::memory_order_release
					);
					return;
				}
				assert(st.shared_cnt == 1);

				// Otherwise, we would not be in state::contended.
				assert(!waiters_.empty());

				// Otherwise, the node would not be waiting (but sharing the lock).
				assert(waiters_.front()->exclusive);

				next = waiters_.pop_front();
				if (waiters_.empty()) {
					// Hand-off to a waiter does not require a fence.
					st_.store(
						state{.c = contention::locked, .shared_cnt = 0},
						std::memory_order_relaxed
					);
				} else {
					// Hand-off to a waiter does not require a fence.
					st_.store(
						state{.c = contention::contended, .shared_cnt = 0},
						std::memory_order_relaxed
					);
				}
			}

			next->complete();
		}

	private:
		platform::mutex mutex_;

		// State transitions are protected by mutex_ except for the transitions:
		// * state::none -> state::locked
		// * state::locked -> state::locked (with different shared_cnt).
		// * state::locked -> state::none
		// which can happen outside of mutex_.
		std::atomic<state> st_{state{.c = contention::none, .shared_cnt = 0}};

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
