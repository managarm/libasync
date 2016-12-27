#ifndef ASYNC_RESULT_HPP
#define ASYNC_RESULT_HPP

#include <atomic>
#include <type_traits>
#include <utility>

#include <async/basic.hpp>

namespace async {

namespace detail {
	template<typename T>
	struct result_base {
		friend void swap(result_base &a, result_base &b) {
			using std::swap;
			swap(a.object, b.object);
		}

		result_base()
		: object(nullptr) { }

		explicit result_base(awaitable<T> *obj)
		: object(obj) { }

		result_base(result_base &&other)
		: result_base() {
			swap(*this, other);
		}

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
		object->then(awaiter);
		object = nullptr;
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
		object->then(awaiter);
		object = nullptr;
	}
};

template<typename T>
struct promise {
	struct state : awaitable<T> {
	private:
		enum : unsigned int {
			has_value = 1,
			has_awaiter = 2
		};

	public:
		state()
		: _flags(0) { }

		void set_value(T value) {
			new (&_value) T{std::move(value)};

			auto f = _flags.fetch_or(has_value, std::memory_order_acq_rel);
			assert(!(f & has_value));
			if(f & has_awaiter && _awaiter) {
				auto vp = reinterpret_cast<T *>(&_value);
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

	promise()
	: _setter(new state), _getter(_setter) { }

	void set_value(T value) {
		assert(_setter);
		_setter->set_value(std::move(value));
		_setter = nullptr;
	}

	result<T> async_get() {
		assert(_getter);
		auto res = result<T>{_getter};
		_getter = nullptr;
		return res;
	}

private:
	state *_setter;
	state *_getter;
};

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

} // namespace async

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
