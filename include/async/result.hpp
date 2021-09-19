#pragma once

#include <atomic>
#include <type_traits>
#include <utility>

#include <async/basic.hpp>

namespace async {

// ----------------------------------------------------------------------------
// Basic result<T> class implementation.
// ----------------------------------------------------------------------------

template<typename T>
struct result_reference {
	result_reference()
	: object{nullptr} { }

	result_reference(awaitable<T> *obj)
	: object{obj} { }

	awaitable<T> *get_awaitable() const {
		return object;
	}

protected:
	awaitable<T> *object;
};

namespace detail {
	template<typename T>
	struct result_promise;

	template<typename T>
	struct result_base {
		friend void swap(result_base &a, result_base &b) {
			using std::swap;
			swap(a.object, b.object);
		}

		result_base()
		: object{nullptr} { }

		result_base(const result_base &) = delete;

		result_base(result_base &&other)
		: result_base() {
			swap(*this, other);
		}

		explicit result_base(awaitable<T> *obj)
		: object{obj} { }

		~result_base() {
			if(object)
				object->drop();
		}

		result_base &operator= (result_base other) {
			swap(*this, other);
			return *this;
		}

		awaitable<T> *get_awaitable() {
			return object;
		}

	protected:
		awaitable<T> *object;
	};
}

template<typename T>
struct [[nodiscard]] result : private detail::result_base<T> {
private:
	using detail::result_base<T>::object;

public:
	using detail::result_base<T>::get_awaitable;

	using value_type = T;

	using promise_type = detail::result_promise<T>;

	result() = default;

	explicit result(awaitable<T> *obj)
	: detail::result_base<T>{obj} { }

	operator result_reference<T> () {
		return result_reference<T>{object};
	}

	bool ready() {
		assert(object);
		return object->ready();
	}

	void then(callback<void()> awaiter) {
		assert(object);
		object->then(awaiter);
	}

	T &value() {
		return object->value();
	}
};

template<>
struct [[nodiscard]] result<void> : private detail::result_base<void> {
private:
	using detail::result_base<void>::object;

public:
	using detail::result_base<void>::get_awaitable;

	using value_type = void;

	using promise_type = detail::result_promise<void>;

	result() = default;

	explicit result(awaitable<void> *obj)
	: detail::result_base<void>{obj} { }

	operator result_reference<void> () {
		return result_reference<void>{object};
	}

	bool ready() {
		assert(object);
		return object->ready();
	}

	void then(callback<void()> awaiter) {
		assert(object);
		object->then(awaiter);
	}
};

namespace detail {
	template<typename T>
	struct result_promise : private async::awaitable<T> {
		result_promise() { }

	private:
		void submit() override {
			auto handle = corons::coroutine_handle<result_promise>::from_promise(*this);
			handle.resume();
		}

		void dispose() override {
			auto handle = corons::coroutine_handle<result_promise>::from_promise(*this);
			handle.destroy();
		}

	public:
		async::result<T> get_return_object() {
			return async::result<T>{this};
		}

		auto initial_suspend() { return corons::suspend_always{}; }

		auto final_suspend() noexcept {
			struct awaiter {
				awaiter(result_promise *p)
				: _p{p} { }

				bool await_ready() noexcept {
					return false;
				}

				void await_suspend(corons::coroutine_handle<>) noexcept {
					_p->set_ready();
				}

				void await_resume() noexcept {
					platform::panic("libasync: Internal fatal error:"
							" Coroutine resumed from final suspension point");
				}

			private:
				result_promise *_p;
			};

			return awaiter{this};
		}

		void return_value(T value) {
			async::awaitable<T>::emplace_value(std::move(value));
		}

		template<typename X>
		void return_value(X &&value) {
			async::awaitable<T>::emplace_value(std::forward<X>(value));
		}

		void unhandled_exception() {
			platform::panic("libasync: Unhandled exception in coroutine");
		}
	};

	template<>
	struct result_promise<void> : private async::awaitable<void> {
		result_promise() { }

	private:
		void submit() override {
			auto handle = corons::coroutine_handle<result_promise>::from_promise(*this);
			handle.resume();
		}

		void dispose() override {
			auto handle = corons::coroutine_handle<result_promise>::from_promise(*this);
			handle.destroy();
		}

	public:
		async::result<void> get_return_object() {
			return async::result<void>{this};
		}

		auto initial_suspend() { return corons::suspend_always{}; }

		auto final_suspend() noexcept {
			struct awaiter {
				awaiter(result_promise *p)
				: _p{p} { }

				bool await_ready() noexcept {
					return false;
				}

				void await_suspend(corons::coroutine_handle<>) noexcept {
					_p->set_ready();
				}

				void await_resume() noexcept {
					platform::panic("libasync: Internal fatal error:"
							" Coroutine resumed from final suspension point");
				}

			private:
				result_promise *_p;
			};

			return awaiter{this};
		}

		void return_void() {
			async::awaitable<void>::emplace_value();
		}

		void unhandled_exception() {
			platform::panic("libasync: Unhandled exception in coroutine");
		}
	};
}

// ----------------------------------------------------------------------------
// Sender/receiver support for result<T>.
// ----------------------------------------------------------------------------

namespace detail {
	template<typename T, typename Receiver>
	struct result_operation {
		result_operation(result<T> res, Receiver rcv)
		: res_{std::move(res)}, rcv_{std::move(rcv)} { }

		result_operation(const result_operation &) = delete;

		result_operation &operator= (const result_operation &) = delete;

		bool start_inline() {
			res_.then([this] () {
				execution::set_value_noinline(rcv_, std::move(res_.value()));
			});
			return false;
		}

	private:
		result<T> res_;
		Receiver rcv_;
	};

	template<typename Receiver>
	struct result_operation<void, Receiver> {
		result_operation(result<void> res, Receiver rcv)
		: res_{std::move(res)}, rcv_{std::move(rcv)} { }

		result_operation(const result_operation &) = delete;

		result_operation &operator= (const result_operation &) = delete;

		bool start_inline() {
			res_.then([this] {
				execution::set_value_noinline(rcv_);
			});
			return false;
		}

	private:
		result<void> res_;
		Receiver rcv_;
	};
};

template<typename T, typename Receiver>
detail::result_operation<T, Receiver> connect(result<T> s, Receiver r) {
	return {std::move(s), std::move(r)};
}

template<typename T>
sender_awaiter<result<T>, T> operator co_await(result<T> res) {
	return {std::move(res)};
};

template<typename S>
async::result<typename S::value_type> make_result(S sender) {
	co_return co_await std::move(sender);
}

} // namespace async
