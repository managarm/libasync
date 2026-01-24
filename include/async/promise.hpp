#pragma once

#include <atomic>
#include <type_traits>
#include <utility>

#include <async/basic.hpp>
#include <async/cancellation.hpp>
#include <async/algorithm.hpp>

#include <frg/functional.hpp>

namespace async {

// ----------------------------------------------------------------------------
// promise<T> and future<T> class implementations.
// ----------------------------------------------------------------------------

template <typename T, typename Allocator>
struct future;

namespace detail {
	struct promise_state_base {
		~promise_state_base() {
			assert(!ctr_);
		}

		void ref() {
			ctr_.fetch_add(1, std::memory_order_acq_rel);
		}

		auto unref() {
			return ctr_.fetch_sub(1, std::memory_order_acq_rel) - 1;
		}

		bool has_value() {
			return has_value_;
		}

		platform::mutex mutex_;

		struct node {
			friend struct promise_state_base;

			node() = default;

			node (const node &) = delete;
			node &operator=(const node &) = delete;

			virtual void complete() = 0;

		protected:
			virtual ~node() = default;

		private:
			frg::default_list_hook<node> hook_;
		};

		frg::intrusive_list<
			node,
			frg::locate_member<
				node,
				frg::default_list_hook<node>,
				&node::hook_
			>
		> queue_;

		void wake() {
			assert(has_value_);
			frg::intrusive_list<
				node,
				frg::locate_member<
					node,
					frg::default_list_hook<node>,
					&node::hook_
				>
			> items;
			{
				frg::unique_lock lock{mutex_};

				items.splice(items.end(), queue_);
			}

			// Now invoke the individual callbacks.
			while(!items.empty()) {
				auto item = items.front();
				items.pop_front();
				item->complete();
			}
		}

		bool has_value_ = false;

	private:
		std::atomic_size_t ctr_ = 1;
	};

	template <typename T>
	struct promise_state : promise_state_base {
		~promise_state() {
			if (has_value_)
				get().~T();
		}

		T &get() {
			assert(has_value_);
			return *std::launder(reinterpret_cast<T *>(stor_.buffer));
		}

		template <typename U>
		void set_value(U &&v) {
			assert(!has_value_);
			new (stor_.buffer) T{std::forward<U>(v)};
			has_value_ = true;
		}
	private:
		frg::aligned_storage<sizeof(T), alignof(T)> stor_;
	};

	template <>
	struct promise_state<void> : promise_state_base {
		void set_value() {
			assert(!has_value_);
			has_value_ = true;
		}
	};

	template <typename T, typename Allocator>
	struct promise_base {
		friend void swap(promise_base &a, promise_base &b) {
			using std::swap;
			swap(a.state_, b.state_);
		}

		promise_base(Allocator alloc = Allocator())
		: state_{frg::construct<promise_state<T>>(alloc)}, alloc_{alloc} { }

		~promise_base() {
			disown_state();
		}

		promise_base(const promise_base &) = delete;

		promise_base(promise_base &&other)
		: state_{nullptr} {
			swap(*this, other);
		}

		promise_base &operator=(promise_base other) {
			swap(*this, other);
			return *this;
		}

		auto get_future() {
			return future<T, Allocator>{state_, alloc_};
		}

	protected:
		void disown_state() {
			if (state_ && !state_->unref()) {
				frg::destruct(alloc_, state_);
			}

			state_ = nullptr;
		}

		promise_state<T> *state_;
		Allocator alloc_;
	};

	template <typename U>
	struct get_value_type {
		using type = frg::optional<U *>;
	};

	template <>
	struct get_value_type<void> {
		using type = bool;
	};
}

template<typename T, typename Allocator>
struct future {
	friend struct detail::promise_base<T, Allocator>;

public:
	friend void swap(future &a, future &b) {
		using std::swap;
		swap(a.state_, b.state_);
		swap(a.alloc_, b.alloc_);
	}

	future(Allocator allocator = Allocator())
	: state_{nullptr}, alloc_{allocator} { }

	~future() {
		if (state_ && !state_->unref())
			frg::destruct(alloc_, state_);
	}

	future(const future &other)
	: state_{other.state_}, alloc_{other.alloc_} {
		if (state_)
			state_->ref();
	}

	future &operator=(const future &other) {
		if (state_) {
			if (!state_->unref())
				frg::destruct(alloc_, state_);
		}

		state_ = other.state_;
		alloc_ = other.alloc_;
		state_->ref();
		return *this;
	}

	future(future &&other)
	: state_{nullptr}, alloc_{} {
		swap(*this, other);
	}

	future &operator=(future &&other) {
		swap(*this, other);
		return *this;
	}

	// ----------------------------------------------------------------------------------
	// get() and its boilerplate.
	// ----------------------------------------------------------------------------------

	template<typename Receiver>
	struct get_operation final : private detail::promise_state_base::node {
		get_operation(detail::promise_state<T> *state, cancellation_token ct, Receiver r)
		: state_{state}, ct_{std::move(ct)}, r_{std::move(r)}, cobs_{this} { }

		void start() {
			bool cancelled = false;
			{
				frg::unique_lock lock{state_->mutex_};

				if (!state_->has_value()) {
					if (!cobs_.try_set(ct_)) {
						cancelled = true;
					} else {
						state_->queue_.push_back(this);
						return;
					}
				}
			}

			if constexpr (std::is_same_v<T, void>)
				return execution::set_value(r_, !cancelled);
			else {
				if (cancelled)
					return execution::set_value(r_, frg::optional<T *>{frg::null_opt});
				else
					return execution::set_value(r_, frg::optional<T *>{&state_->get()});
			}
		}

	private:
		void cancel() {
			bool cancelled = false;
			{
				frg::unique_lock lock{state_->mutex_};

				if (!state_->has_value()) {
					cancelled = true;
					auto it = state_->queue_.iterator_to(this);
					state_->queue_.erase(it);
				}
			}

			if constexpr (std::is_same_v<T, void>)
				execution::set_value(r_, !cancelled);
			else {
				if (cancelled)
					execution::set_value(r_, frg::optional<T *>{frg::null_opt});
				else
					execution::set_value(r_, frg::optional<T *>{&state_->get()});
			}
		}

		void complete() override {
			if (cobs_.try_reset()) {
				if constexpr (std::is_same_v<T, void>)
					execution::set_value(r_, true);
				else
					execution::set_value(r_, frg::optional<T *>{&state_->get()});
			}
		}

		detail::promise_state<T> *state_;
		cancellation_token ct_;
		Receiver r_;
		cancellation_observer<frg::bound_mem_fn<&get_operation::cancel>> cobs_;
	};

public:
	struct [[nodiscard]] get_sender {
		using value_type = typename detail::get_value_type<T>::type;

		template <typename Receiver>
		friend get_operation<Receiver> connect(get_sender s, Receiver r) {
			return {s.state, s.ct, std::move(r)};
		}

		sender_awaiter<get_sender, value_type> operator co_await() {
			return {*this};
		}

		detail::promise_state<T> *state;
		cancellation_token ct;
	};

	get_sender get(cancellation_token ct) {
		return {state_, ct};
	}

	decltype(auto) get() {
		if constexpr (std::is_same_v<T, void>) {
			return async::transform(get(cancellation_token{}), [] (bool v) -> void {
				assert(v);
			});
		} else {
			return async::transform(get(cancellation_token{}), [] (frg::optional<T *> v) -> T * {
				assert(v);
				return *v;
			});
		}
	}

	bool valid() const {
		return state_;
	}

	operator bool () const {
		return valid();
	}

private:
	future(detail::promise_state<T> *state, Allocator alloc)
	: state_{state}, alloc_{alloc} {
		if (state_)
			state_->ref();
	}

	detail::promise_state<T> *state_;
	Allocator alloc_;
};

template<typename T, typename Allocator>
struct promise : private detail::promise_base<T, Allocator> {
private:
	using detail::promise_base<T, Allocator>::disown_state;
	using detail::promise_base<T, Allocator>::state_;

public:
	using detail::promise_base<T, Allocator>::get_future;

	template <typename U>
	void set_value(U &&v) {
		{
			frg::unique_lock lock{state_->mutex_};
			state_->set_value(std::forward<U>(v));
		}

		state_->wake();
		disown_state();
	}
};

template<typename Allocator>
struct promise<void, Allocator> : private detail::promise_base<void, Allocator> {
private:
	using detail::promise_base<void, Allocator>::disown_state;
	using detail::promise_base<void, Allocator>::state_;

public:
	using detail::promise_base<void, Allocator>::get_future;

	void set_value() {
		{
			frg::unique_lock lock{state_->mutex_};
			state_->set_value();
		}

		state_->wake();
		disown_state();
	}
};

} // namespace async
