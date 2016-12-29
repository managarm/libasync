#ifndef ASYNC_RESULT_HPP
#define ASYNC_RESULT_HPP

#include <assert.h>
#include <atomic>
#include <type_traits>
#include <utility>

#include <async/basic.hpp>
#include <cofiber.hpp>

namespace async {

// ----------------------------------------------------------------------------
// Basic result<T> class implementation.
// ----------------------------------------------------------------------------

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
		: object(obj) { }

		~result_base() {
			if(object)
				object->detach();
		}

		result_base &operator= (result_base other) {
			swap(*this, other);
			return *this;
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
	explicit result(awaitable<T> *obj)
	: detail::result_base<T>(obj) { }

	void then(callback<void(T)> awaiter) {
		assert(object);
		std::exchange(object, nullptr)->then(awaiter);
	}
};

template<>
struct result<void> : private detail::result_base<void> {
private:
	using detail::result_base<void>::object;

public:
	explicit result(awaitable<void> *object)
	: detail::result_base<void>(object) { }

	void then(callback<void()> awaiter) {
		assert(object);
		std::exchange(object, nullptr)->then(awaiter);
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
			return false;
		}

		template<typename H>
		void await_suspend(H handle) {
			_address = handle.address();
			_res.then([this] (T value) {
				new (&_value) T{std::move(value)};
				H::from_address(_address).resume();
			});
		}

		T await_resume() {
			auto p = reinterpret_cast<T *>(&_value);
			T value = std::move(*p);
			p->~T();
			return value;
		}

	private:
		result<T> _res;
		void *_address;
		std::aligned_storage_t<sizeof(T), alignof(T)> _value;
	};
	
	template<>
	struct result_awaiter<void> {
		result_awaiter(result<void> res)
		: _res{std::move(res)} { }

		result_awaiter(const result_awaiter &) = delete;

		result_awaiter &operator= (const result_awaiter &) = delete;

		bool await_ready() {
			return false;
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
	public:
		promise_state()
		: _flags(0) { }

		void set_value(T value) {
			new (&_value) T{std::move(value)};

			auto f = _flags.fetch_or(has_value, std::memory_order_acq_rel);
			assert(!(f & has_value));
			if(f & has_awaiter && _awaiter) {
				auto vp = reinterpret_cast<T *>(&_value);
				if(_awaiter)
					_awaiter(std::move(*vp));
				vp->~T();
				delete this;
			}
		}

		void then(callback<void(T)> awaiter) override {
			_awaiter = awaiter;

			auto f = _flags.fetch_or(has_awaiter, std::memory_order_acq_rel);
			assert(!(f & has_awaiter));
			if(f & has_value) {
				auto vp = reinterpret_cast<T *>(&_value);
				_awaiter(std::move(*vp));
				vp->~T();
				delete this;
			}
		}

		void detach() override {
			auto f = _flags.fetch_or(has_awaiter, std::memory_order_acq_rel);
			assert(!(f & has_awaiter));
			if(f & has_value) {
				auto vp = reinterpret_cast<T *>(&_value);
				vp->~T();
				delete this;
			}
		}

	private:
		std::atomic<unsigned int> _flags;
		std::aligned_storage_t<sizeof(T), alignof(T)> _value;
		callback<void(T)> _awaiter;
	};
	
	template<>
	struct promise_state<void> : awaitable<void> {
		promise_state()
		: _flags(0) { }

		void set_value() {
			auto f = _flags.fetch_or(has_value, std::memory_order_acq_rel);
			assert(!(f & has_value));
			if(f & has_awaiter) {
				if(_awaiter)
					_awaiter();
				delete this;
			}
		}

		void then(callback<void()> awaiter) override {
			_awaiter = awaiter;

			auto f = _flags.fetch_or(has_awaiter, std::memory_order_acq_rel);
			assert(!(f & has_awaiter));
			if(f & has_value) {
				_awaiter();
				delete this;
			}
		}

		void detach() override {
			auto f = _flags.fetch_or(has_awaiter, std::memory_order_acq_rel);
			assert(!(f & has_awaiter));
			if(f & has_value)
				delete this;
		}

	private:
		std::atomic<unsigned int> _flags;
		callback<void()> _awaiter;
	};

	template<typename T>
	struct promise_base {
		friend void swap(promise_base &a, promise_base &b) {
			using std::swap;
			swap(a.setter, b.setter);
			swap(a.getter, b.getter);
		}

		promise_base()
		: setter(new promise_state<T>), getter(setter) { }

		promise_base(const promise_base &) = delete;

		promise_base(promise_base &&other)
		: setter(nullptr), getter(nullptr) {
			swap(*this, other);
		}

		~promise_base() {
			assert(!setter && !getter);
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
		std::exchange(setter, nullptr)->set_value(std::move(value));
	}

	result<T> async_get() {
		assert(getter);
		auto res = result<T>{std::exchange(getter, nullptr)};
		return res;
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
		std::exchange(setter, nullptr)->set_value();
	}

	result<void> async_get() {
		assert(getter);
		auto res = result<void>{std::exchange(getter, nullptr)};
		return res;
	}
};

} // namespace async

// ----------------------------------------------------------------------------
// Support for using result<T> as a coroutine return type.
// ----------------------------------------------------------------------------

namespace cofiber {
	template<typename T>
	struct coroutine_traits<async::result<T>> {
		struct promise_type {
			async::result<T> get_return_object(coroutine_handle<> handle) {
				return _promise.async_get();
			}

			auto initial_suspend() { return suspend_never(); }
			auto final_suspend() { return suspend_never(); }

			template<typename... V>
			void return_value(V &&... value) {
				_promise.set_value(std::forward<V>(value)...);
			}

		private:
			async::promise<T> _promise;
		};
	};
}

#endif // ASYNC_RESULT_HPP
