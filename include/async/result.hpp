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
	
	template<typename T>
	struct cancelable_result_base {
		friend void swap(cancelable_result_base &a, cancelable_result_base &b) {
			using std::swap;
			swap(a.object, b.object);
		}

		cancelable_result_base()
		: object{nullptr} { }
		
		cancelable_result_base(const cancelable_result_base &) = delete;

		cancelable_result_base(cancelable_result_base &&other)
		: cancelable_result_base() {
			swap(*this, other);
		}

		explicit cancelable_result_base(cancelable_awaitable<T> *obj)
		: object(obj) { }

		~cancelable_result_base() {
			if(object)
				object->detach();
		}

		cancelable_result_base &operator= (cancelable_result_base other) {
			swap(*this, other);
			return *this;
		}

	protected:
		cancelable_awaitable<T> *object;
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

template<typename T>
struct cancelable_result : private detail::cancelable_result_base<T> {
private:
	using detail::cancelable_result_base<T>::object;

public:
	cancelable_result() = default;

	explicit cancelable_result(cancelable_awaitable<T> *obj)
	: detail::cancelable_result_base<T>(obj) { }
	
	operator result<T> () && {
		return result<T>{std::exchange(object, nullptr)};
	}

	void then(callback<void(T)> awaiter) {
		assert(object);
		std::exchange(object, nullptr)->then(awaiter);
	}

	void cancel() {
		assert(object);
		object->cancel();
	}
};

template<>
struct cancelable_result<void> : private detail::cancelable_result_base<void> {
private:
	using detail::cancelable_result_base<void>::object;

public:
	cancelable_result() = default;

	explicit cancelable_result(cancelable_awaitable<void> *object)
	: detail::cancelable_result_base<void>(object) { }
	
	operator result<void> () && {
		return result<void>{std::exchange(object, nullptr)};
	}

	void then(callback<void()> awaiter) {
		assert(object);
		std::exchange(object, nullptr)->then(awaiter);
	}

	void cancel() {
		assert(object);
		object->cancel();
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

template<typename T>
async::detail::result_awaiter<T> cofiber_awaiter(async::cancelable_result<T> res) {
	return {async::result<T>{std::move(res)}};
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
		std::exchange(setter, nullptr)->set_value();
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

		result<T> async_get() {
			assert(!std::exchange(_retrieved, true));
			return result<T>{this};
		}

	protected:
		bool _retrieved;
	};

	template<typename T>
	struct pledge : private pledge_base<T> {
		pledge()
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

		using pledge_base<void>::async_get;

	private:
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

		std::atomic<unsigned int> _flags;
		std::aligned_storage_t<sizeof(T), alignof(T)> _value;
		callback<void(T)> _awaiter;
	};

	template<>
	struct pledge<void> : private pledge_base<void> {
		pledge()
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

		using pledge_base<void>::async_get;

	private:
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

		std::atomic<unsigned int> _flags;
		callback<void()> _awaiter;
	};
}

using detail::pledge;

} // namespace async

// ----------------------------------------------------------------------------
// Support for using result<T> as a coroutine return type.
// ----------------------------------------------------------------------------

namespace async {
	struct cancel_future_t { };

	inline constexpr cancel_future_t cancel_future;
}

namespace cofiber {
	template<typename T>
	struct coroutine_traits<async::result<T>> {
		struct promise_type {

			async::result<T> get_return_object(coroutine_handle<> handle) {
				return _promise.async_get();
			}

			auto initial_suspend() { return suspend_never(); }
			auto final_suspend() { return suspend_always(); }

			template<typename... V>
			void return_value(V &&... value) {
				_promise.set_value(std::forward<V>(value)...);
			}

		private:
			async::promise<T> _promise;
		};
	};
	
	template<typename T>
	struct coroutine_traits<async::cancelable_result<T>> {
		struct promise_type : async::cancelable_awaitable<T> {
		private:
			struct cancel_state_base {
				cancel_state_base()
				: ready{false}, cancelled{false}, waiting{false} { }

				bool ready;
				bool cancelled;
				bool waiting;
				cofiber::coroutine_handle<> handle;
			};

			template<typename X>
			struct cancel_state : cancel_state_base {
				std::experimental::optional<X> result;
			};

		public:
			template<typename X>
			struct cancellation {
				struct awaiter {
					awaiter(cancel_state<X> *st)
					: _st{st}, _fastpath{false} { }

					bool await_ready() {
						_fastpath = _st->ready;
						return _fastpath;
					}

					void await_suspend(cofiber::coroutine_handle<> handle) {
						assert(!_fastpath);
						assert(!_st->waiting);
						_st->waiting = true;
						_st->handle = handle;
					}

					X await_resume() {
						if(!_fastpath) {
							assert(_st->waiting);
							_st->waiting = false;
						}

						return std::move(_st->result.value());
					}

				private:
					cancel_state<X> *_st;
					bool _fastpath;
				};

				friend auto cofiber_awaiter(cancellation &cnl) {
					return awaiter{cnl._st};
				}

				cancellation(cancel_state<X> *st)
				: _st{st} { }

				bool cancelled() {
					return _st->cancelled;
				}

			private:
				cancel_state<X> *_st;
			};

			template<typename X>
			struct yield_awaiter {
				yield_awaiter(promise_type *p, async::result<X> result)
				: _p{p} {
					_st = new cancel_state<X>;

					result.then([st = _st] (X result) {
						st->result = std::move(result);
						st->ready = true;
						if(st->waiting)
							st->handle.resume();
					});
				}

				bool await_ready() {
					return false;
				}

				void await_suspend(cofiber::coroutine_handle<> handle) {
					assert(!_p->_yield_state);
					_p->_yield_state = _st;

					assert(!_st->waiting);
					_st->waiting = true;
					_st->handle = handle;
				}

				cancellation<X> await_resume() {
					assert(_p->_yield_state == _st);
					_p->_yield_state = nullptr;

					assert(_st->waiting);
					_st->waiting = false;

					return cancellation<X>{_st};
				}
			
			private:
				promise_type *_p;
				cancel_state<X> *_st;
			};

			promise_type()
			: _future{_promise.async_get()}, _yield_state{nullptr} { }

			async::cancelable_result<T> get_return_object(coroutine_handle<> handle) {
				return async::cancelable_result<T>{this};
			}

			// TODO: We need to suspend_always on final suspend to get the lifetime
			// of cancel() correct!
			auto initial_suspend() { return suspend_never(); }
			auto final_suspend() { return suspend_always(); }

			template<typename... V>
			void return_value(V &&... value) {
				_promise.set_value(std::forward<V>(value)...);
			}

			template<typename X>
			yield_awaiter<X> yield_value(async::result<X> result) {
				return yield_awaiter<X>{this, std::move(result)};
			}

			void then(async::callback<void(T)> awaiter) override {
				_future.then(awaiter);
			}

			void detach() override {
			}

			void cancel() override {
				if(_yield_state) {
					_yield_state->cancelled = true;
					if(_yield_state->waiting)
						_yield_state->handle.resume();
				}
			}

		private:
			// TODO: Do not use promise here!
			async::promise<T> _promise;
			async::result<T> _future;
			cancel_state_base *_yield_state;
		};
	};
	
	template<>
	struct coroutine_traits<async::cancelable_result<void>> {
		struct promise_type : async::cancelable_awaitable<void> {
		private:
			struct cancel_state_base {
				cancel_state_base()
				: ready{false}, cancelled{false}, waiting{false} { }

				bool ready;
				bool cancelled;
				bool waiting;
				cofiber::coroutine_handle<> handle;
			};

			template<typename X>
			struct cancel_state : cancel_state_base {
				std::experimental::optional<X> result;
			};

		public:
			template<typename X>
			struct cancellation {
				struct awaiter {
					awaiter(cancel_state<X> *st)
					: _st{st}, _fastpath{false} { }

					bool await_ready() {
						_fastpath = _st->ready;
						return _fastpath;
					}

					void await_suspend(cofiber::coroutine_handle<> handle) {
						assert(!_fastpath);
						assert(!_st->waiting);
						_st->waiting = true;
						_st->handle = handle;
					}

					X await_resume() {
						if(!_fastpath) {
							assert(_st->waiting);
							_st->waiting = false;
						}

						return std::move(_st->result.value());
					}

				private:
					cancel_state<X> *_st;
					bool _fastpath;
				};

				friend auto cofiber_awaiter(cancellation &cnl) {
					return awaiter{cnl._st};
				}

				cancellation(cancel_state<X> *st)
				: _st{st} { }

				bool cancelled() {
					return _st->cancelled;
				}

			private:
				cancel_state<X> *_st;
			};

			template<typename X>
			struct yield_awaiter {
				yield_awaiter(promise_type *p, async::result<X> result)
				: _p{p} {
					_st = new cancel_state<X>;

					result.then([st = _st] (X result) {
						st->result = std::move(result);
						st->ready = true;
						if(st->waiting)
							st->handle.resume();
					});
				}

				bool await_ready() {
					return false;
				}

				void await_suspend(cofiber::coroutine_handle<> handle) {
					assert(!_p->_yield_state);
					_p->_yield_state = _st;

					assert(!_st->waiting);
					_st->waiting = true;
					_st->handle = handle;
				}

				cancellation<X> await_resume() {
					assert(_p->_yield_state == _st);
					_p->_yield_state = nullptr;

					assert(_st->waiting);
					_st->waiting = false;

					return cancellation<X>{_st};
				}
			
			private:
				promise_type *_p;
				cancel_state<X> *_st;
			};

			promise_type()
			: _future{_promise.async_get()}, _yield_state{nullptr} { }

			async::cancelable_result<void> get_return_object(coroutine_handle<> handle) {
				return async::cancelable_result<void>{this};
			}

			// TODO: We need to suspend_always on final suspend to get the lifetime
			// of cancel() correct!
			auto initial_suspend() { return suspend_never(); }
			auto final_suspend() { return suspend_always(); }

			template<typename... V>
			void return_value(V &&... value) {
				_promise.set_value(std::forward<V>(value)...);
			}

			template<typename X>
			yield_awaiter<X> yield_value(async::result<X> result) {
				return yield_awaiter<X>{this, std::move(result)};
			}

			void then(async::callback<void()> awaiter) override {
				_future.then(awaiter);
			}

			void detach() override {
			}

			void cancel() override {
				if(_yield_state) {
					_yield_state->cancelled = true;
					if(_yield_state->waiting)
						_yield_state->handle.resume();
				}
			}

		private:
			// TODO: Do not use promise here!
			async::promise<void> _promise;
			async::result<void> _future;
			cancel_state_base *_yield_state;
		};
	};
}

#endif // ASYNC_RESULT_HPP
