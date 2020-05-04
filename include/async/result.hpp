#ifndef ASYNC_RESULT_HPP
#define ASYNC_RESULT_HPP

#include <atomic>
#include <experimental/coroutine>
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
struct result : private detail::result_base<T> {
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
struct result<void> : private detail::result_base<void> {
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
			auto handle = std::experimental::coroutine_handle<result_promise>::from_promise(*this);
			handle.resume();
		}

		void dispose() override {
			auto handle = std::experimental::coroutine_handle<result_promise>::from_promise(*this);
			handle.destroy();
		}

	public:
		async::result<T> get_return_object() {
			return async::result<T>{this};
		}

		auto initial_suspend() { return std::experimental::suspend_always{}; }

		auto final_suspend() {
			struct awaiter {
				awaiter(result_promise *p)
				: _p{p} { }

				bool await_ready() {
					return false;
				}

				void await_suspend(std::experimental::coroutine_handle<>) {
					_p->set_ready();
				}

				void await_resume() {
					platform::panic("libasync: Internal fatal error:"
							" Coroutine resumed from final suspension point");
				}

			private:
				result_promise *_p;
			};

			return awaiter{this};
		}

		template<typename... V>
		void return_value(V &&... value) {
			async::awaitable<T>::emplace_value(std::forward<V>(value)...);
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
			auto handle = std::experimental::coroutine_handle<result_promise>::from_promise(*this);
			handle.resume();
		}

		void dispose() override {
			auto handle = std::experimental::coroutine_handle<result_promise>::from_promise(*this);
			handle.destroy();
		}

	public:
		async::result<void> get_return_object() {
			return async::result<void>{this};
		}

		auto initial_suspend() { return std::experimental::suspend_always{}; }

		auto final_suspend() {
			struct awaiter {
				awaiter(result_promise *p)
				: _p{p} { }

				bool await_ready() {
					return false;
				}

				void await_suspend(std::experimental::coroutine_handle<>) {
					_p->set_ready();
				}

				void await_resume() {
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

		void start() {
			res_.then([this] () {
				rcv_.set_value(std::move(res_.value()));
			});
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

		void start() {
			res_.then([this] {
				rcv_.set_value();
			});
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

// ----------------------------------------------------------------------------
// promise<T> class implementation.
// ----------------------------------------------------------------------------

namespace detail {
	enum : unsigned int {
		has_value = 1,
		has_awaiter = 2
	};

	template<typename T>
	struct promise_state : awaitable<T> {
		using awaitable<T>::emplace_value;
		using awaitable<T>::set_ready;

		void submit() override {
			_active = true;
			if(_raised)
				set_ready();
		}

		void dispose() override {
			// TODO: Review assertions here.
			delete this;
		}

		void raise() {
			_raised = true;
			if(_active)
				set_ready();
		}

	private:
		bool _active = false;
		bool _raised = false;
	};

	template<>
	struct promise_state<void> : awaitable<void> {
		using awaitable<void>::set_ready;

		void submit() override {
			_active = true;
			if(_raised)
				set_ready();
		}

		void dispose() override {
			// TODO: Review assertions here.
			delete this;
		}

		void raise() {
			_raised = true;
			if(_active)
				set_ready();
		}

	private:
		bool _active = false;
		bool _raised = false;
	};

	template<typename T>
	struct promise_base {
		friend void swap(promise_base &a, promise_base &b) {
			using std::swap;
			swap(a.setter, b.setter);
			swap(a.getter, b.getter);
		}

		promise_base() {
			auto st = new promise_state<T>{};
			setter = st;
			getter = st;
		}

		promise_base(const promise_base &) = delete;

		promise_base(promise_base &&other)
		: setter(nullptr), getter(nullptr) {
			swap(*this, other);
		}

		~promise_base() {
			// TODO: Review this condition.
			//assert(!setter && !getter);
		}

		promise_base &operator= (promise_base other) {
			swap(*this, other);
			return *this;
		}

	protected:
		promise_state<T> *setter;
		promise_state<T> *getter;
	};
}

template<typename T>
struct promise : private detail::promise_base<T> {
private:
	using detail::promise_base<T>::setter;
	using detail::promise_base<T>::getter;

public:
	void set_value(T value) {
		assert(setter);
		auto s = std::exchange(setter, nullptr);
		s->emplace_value(std::move(value));
		s->raise();
	}

	result<T> async_get() {
		assert(getter);
		return result<T>{std::exchange(getter, nullptr)};
	}
};

template<>
struct promise<void> : private detail::promise_base<void> {
private:
	using detail::promise_base<void>::setter;
	using detail::promise_base<void>::getter;

public:
	void set_value() {
		assert(setter);
		auto s = std::exchange(setter, nullptr);
		s->raise();
	}

	result<void> async_get() {
		assert(getter);
		return result<void>{std::exchange(getter, nullptr)};
	}
};

// ----------------------------------------------------------------------------
// pledge<T> class implementation.
// ----------------------------------------------------------------------------

namespace detail {
	template<typename T>
	struct pledge_base : awaitable<T> {
		pledge_base()
		: _retrieved(false) { }

		pledge_base(const pledge_base &) = delete;

		~pledge_base() {
			assert(_retrieved);
		}

		pledge_base &operator= (const pledge_base &other) = delete;

		void submit() override {
			_active = true;
			if(_raised)
				awaitable<T>::set_ready();
		}

		void dispose() override {
			// TODO
			//std::cout << "libasync: Handle dispose() for pledge" << std::endl;
		}

		result<T> async_get() {
			assert(!std::exchange(_retrieved, true));
			return result<T>{this};
		}

		void set_ready() {
			_raised = true;
			if(_active)
				awaitable<T>::set_ready();
		}

	protected:
		bool _retrieved;
		bool _active = false;
		bool _raised = false;
	};

	template<typename T>
	struct pledge : private pledge_base<T> {
		using pledge_base<void>::emplace_value;
		using pledge_base<void>::async_get;
	};

	template<>
	struct pledge<void> : private pledge_base<void> {
		using pledge_base<void>::async_get;
	};
}

using detail::pledge;

// TODO: Support non-void results.
template<typename A>
async::result<void> make_result(A awaitable) {
	co_await std::forward<A>(awaitable);
}

} // namespace async

#endif // ASYNC_RESULT_HPP
