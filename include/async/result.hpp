#pragma once

#include <atomic>
#include <type_traits>
#include <utility>

#include <async/basic.hpp>

namespace async {

// ----------------------------------------------------------------------------
// result<T> class implementation.
// ----------------------------------------------------------------------------

template<typename T>
struct result_continuation {
	void pass_value(T value) {
		obj_.emplace(std::move(value));
	}

	template <typename X>
	void emplace_value(X &&value) {
		obj_.emplace(std::forward<X>(value));
	}

	virtual void resume() = 0;

protected:
	T &value() {
		return *obj_;
	}

	~result_continuation() = default;

private:
	frg::optional<T> obj_;
};

// Specialization for coroutines without results.
template<>
struct result_continuation<void> {
	virtual void resume() = 0;

protected:
	~result_continuation() = default;
};

// "Control flow path" that the coroutine takes. This state is used to distinguish inline
// completion from asynchronous completion. In contrast to other state machines, the states
// are not significant only their own; only transitions matter:
// On past_suspend -> past_start transitions, we continue inline.
// On past_start -> past_suspend transitions, we call resume().
enum class coroutine_cfp {
	indeterminate,
	past_start, // We are past start().
	past_suspend // We are past final_suspend.
};

template<typename T, typename R>
struct result_operation;

template<typename T>
struct result {
	template<typename T_, typename R>
	friend struct result_operation;

	using value_type = T;

	struct promise_type {
		template<typename T_, typename R>
		friend struct result_operation;

		result get_return_object() {
			return {corons::coroutine_handle<promise_type>::from_promise(*this)};
		}

		void unhandled_exception() {
			platform::panic("libasync: Unhandled exception in coroutine");
		}

		void return_value(T value) {
			cont_->pass_value(std::move(value));
		}

		template <typename X>
		void return_value(X &&value) {
			cont_->emplace_value(std::forward<X>(value));
		}

		auto initial_suspend() {
			struct awaiter {
				awaiter(promise_type *promise)
				: promise_{promise} { }

				bool await_ready() {
					return false;
				}

				void await_suspend(corons::coroutine_handle<void>) {
					// Do nothing.
				}

				void await_resume() {
					assert(promise_->cont_);
				}

			private:
				promise_type *promise_;
			};
			return awaiter{this};
		}

		auto final_suspend() noexcept {
			struct awaiter {
				awaiter(promise_type *promise)
				: promise_{promise} { }

				bool await_ready() noexcept {
					return false;
				}

				void await_suspend(corons::coroutine_handle<void>) noexcept {
					auto cfp = promise_->cfp_.exchange(coroutine_cfp::past_suspend,
							std::memory_order_release);
					if(cfp == coroutine_cfp::past_start) {
						// We do not need to synchronize with the thread that started the
						// coroutine here, as that thread is already done on its part.
						promise_->cont_->resume();
					}
				}

				void await_resume() noexcept {
					platform::panic("libasync: Internal fatal error: Coroutine resumed from final suspension point");
				}

			private:
				promise_type *promise_;
			};
			return awaiter{this};
		}

	private:
		result_continuation<T> *cont_ = nullptr;
		std::atomic<coroutine_cfp> cfp_{coroutine_cfp::indeterminate};
	};

	result()
	: h_{} { }

	result(corons::coroutine_handle<promise_type> h)
	: h_{h} { }

	result(const result &) = delete;

	result(result &&other)
	: result{} {
		std::swap(h_, other.h_);
	}

	~result() {
		if(h_)
			h_.destroy();
	}

	result &operator= (result other) {
		std::swap(h_, other.h_);
		return *this;
	}

private:
	corons::coroutine_handle<promise_type> h_;
};


// Specialization for coroutines without results.
template<>
struct result<void> {
	template<typename T_, typename R>
	friend struct result_operation;

	using value_type = void;

	struct promise_type {
		template<typename T_, typename R>
		friend struct result_operation;

		result get_return_object() {
			return {corons::coroutine_handle<promise_type>::from_promise(*this)};
		}

		void unhandled_exception() {
			platform::panic("libasync: Unhandled exception in coroutine");
		}

		void return_void() {
			// Do nothing.
		}

		auto initial_suspend() {
			struct awaiter {
				awaiter(promise_type *promise)
				: promise_{promise} { }

				bool await_ready() {
					return false;
				}

				void await_suspend(corons::coroutine_handle<void>) {
					// Do nothing.
				}

				void await_resume() {
					assert(promise_->cont_);
				}

			private:
				promise_type *promise_;
			};
			return awaiter{this};
		}

		auto final_suspend() noexcept {
			struct awaiter {
				awaiter(promise_type *promise)
				: promise_{promise} { }

				bool await_ready() noexcept {
					return false;
				}

				void await_suspend(corons::coroutine_handle<void>) noexcept {
					auto cfp = promise_->cfp_.exchange(coroutine_cfp::past_suspend,
							std::memory_order_release);
					if(cfp == coroutine_cfp::past_start) {
						// We do not need to synchronize with the thread that started the
						// coroutine here, as that thread is already done on its part.
						promise_->cont_->resume();
					}
				}

				void await_resume() noexcept {
					platform::panic("libasync: Internal fatal error: Coroutine resumed from final suspension point");
				}

			private:
				promise_type *promise_;
			};
			return awaiter{this};
		}

	private:
		result_continuation<void> *cont_ = nullptr;
		std::atomic<coroutine_cfp> cfp_{coroutine_cfp::indeterminate};
	};

	result()
	: h_{} { }

	result(corons::coroutine_handle<promise_type> h)
	: h_{h} { }

	result(const result &) = delete;

	result(result &&other)
	: result{} {
		std::swap(h_, other.h_);
	}

	~result() {
		if(h_)
			h_.destroy();
	}

	result &operator= (result other) {
		std::swap(h_, other.h_);
		return *this;
	}

private:
	corons::coroutine_handle<promise_type> h_;
};

template<typename T, typename R>
struct result_operation final : private result_continuation<T> {
	result_operation(result<T> s, R receiver)
	: s_{std::move(s)}, receiver_{std::move(receiver)} { }

	result_operation(const result_operation &) = delete;

	result_operation &operator= (const result_operation &) = delete;

	void start() {
		auto h = s_.h_;
		auto promise = &h.promise();
		promise->cont_ = this;
		h.resume();
		auto cfp = promise->cfp_.exchange(coroutine_cfp::past_start, std::memory_order_relaxed);
		if(cfp == coroutine_cfp::past_suspend) {
			// Synchronize with the thread that complete the coroutine.
			std::atomic_thread_fence(std::memory_order_acquire);
			return async::execution::set_value(receiver_, std::move(value()));
		}
	}

private:
	void resume() override {
		async::execution::set_value_noinline(receiver_, std::move(value()));
	}

private:
	using result_continuation<T>::value;

	result<T> s_;
	R receiver_;
};

// Specialization for coroutines without results.
template<typename R>
struct result_operation<void, R> final : private result_continuation<void> {
	result_operation(result<void> s, R receiver)
	: s_{std::move(s)}, receiver_{std::move(receiver)} { }

	result_operation(const result_operation &) = delete;

	result_operation &operator= (const result_operation &) = delete;

	void start() {
		auto h = s_.h_;
		auto promise = &h.promise();
		promise->cont_ = this;
		h.resume();
		auto cfp = promise->cfp_.exchange(coroutine_cfp::past_start, std::memory_order_relaxed);
		if(cfp == coroutine_cfp::past_suspend) {
			// Synchronize with the thread that complete the coroutine.
			std::atomic_thread_fence(std::memory_order_acquire);
			return async::execution::set_value(receiver_);
		}
	}

private:
	void resume() override {
		async::execution::set_value_noinline(receiver_);
	}

private:
	result<void> s_;
	R receiver_;
};

template<typename T, typename R>
result_operation<T, R> connect(result<T> s, R receiver) {
	return {std::move(s), std::move(receiver)};
};

template<typename T>
async::sender_awaiter<result<T>, T> operator co_await(result<T> s) {
	return {std::move(s)};
}

template<typename S>
async::result<typename S::value_type> make_result(S sender) {
	co_return co_await std::move(sender);
}

} // namespace async
