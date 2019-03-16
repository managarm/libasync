#ifndef ASYNC_RESULT_HPP
#define ASYNC_RESULT_HPP

#include <assert.h>
#include <atomic>
#include <experimental/optional>
#include <iostream>
#include <type_traits>
#include <utility>

#include <async/basic.hpp>
#include <cofiber.hpp>

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

// ----------------------------------------------------------------------------
// co_await support for result<T>.
// ----------------------------------------------------------------------------

namespace detail {
	template<typename T>
	struct result_awaiter {
		result_awaiter(result<T> res)
		: _res{std::move(res)} { }

		result_awaiter(const result_awaiter &) = delete;

		result_awaiter &operator= (const result_awaiter &) = delete;

		bool await_ready() {
			return _res.ready();
		}

		template<typename H>
		void await_suspend(H handle) {
			_address = handle.address();
			_res.then([this] () {
				H::from_address(_address).resume();
			});
		}

		T await_resume() {
			assert(_res.ready());
			return std::move(_res.value());
		}

	private:
		result<T> _res;
		void *_address;
	};
	
	template<>
	struct result_awaiter<void> {
		result_awaiter(result<void> res)
		: _res{std::move(res)} { }

		result_awaiter(const result_awaiter &) = delete;

		result_awaiter &operator= (const result_awaiter &) = delete;

		bool await_ready() {
			return _res.ready();
		}

		template<typename H>
		void await_suspend(H handle) {
			_address = handle.address();
			_res.then([this] {
				H::from_address(_address).resume();
			});
		}

		void await_resume() { }

	private:
		result<void> _res;
		void *_address;
	};
};

template<typename T>
async::detail::result_awaiter<T> cofiber_awaiter(async::result<T> res) {
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

		void dispose() override {
			// TODO: Review assertions here.
			delete this;
		}
	};
	
	template<>
	struct promise_state<void> : awaitable<void> {
		using awaitable<void>::set_ready;

		void dispose() override {
			// TODO: Review assertions here.
			delete this;
		}
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
		s->set_ready();
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
		s->set_ready();
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

		void dispose() override {
			std::cout << "libasync: Handle dispose() for pledge" << std::endl;
		}

		result<T> async_get() {
			assert(!std::exchange(_retrieved, true));
			return result<T>{this};
		}

	protected:
		bool _retrieved;
	};

	template<typename T>
	struct pledge : private pledge_base<T> {
		using pledge_base<void>::emplace_value;
		using pledge_base<void>::async_get;
	};

	template<>
	struct pledge<void> : private pledge_base<void> {
		using pledge_base<void>::set_ready;
		using pledge_base<void>::async_get;
	};
}

using detail::pledge;

} // namespace async

// ----------------------------------------------------------------------------
// Support for using result<T> as a coroutine return type.
// ----------------------------------------------------------------------------

namespace cofiber {
	template<typename T>
	struct coroutine_traits<async::result<T>> {
		struct promise_type : private async::awaitable<T> {
			promise_type() { }

		private:
			void dispose() override {
				auto handle = coroutine_handle<promise_type>::from_promise(*this);
				handle.destroy();
			}

		public:
			async::result<T> get_return_object(coroutine_handle<>) {
				return async::result<T>{this};
			}

			auto initial_suspend() { return suspend_never{}; }

			auto final_suspend() {
				struct awaiter {
					awaiter(promise_type *p)
					: _p{p} { }

					bool await_ready() {
						return false;
					}

					void await_suspend(cofiber::coroutine_handle<>) {
						_p->set_ready();
					}

					void await_resume() {
						std::cerr << "libasync: Internal fatal error:"
								" Coroutine resumed from final suspension point." << std::endl;
						std::terminate();
					}

				private:
					promise_type *_p;
				};

				return awaiter{this};
			}

			template<typename... V>
			void return_value(V &&... value) {
				async::awaitable<T>::emplace_value(std::forward<V>(value)...);
			}
		};
	};
}

#endif // ASYNC_RESULT_HPP
