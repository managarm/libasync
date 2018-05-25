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
		: object{obj} { }

		~cancelable_result_base() {
			if(object)
				object->drop();
		}

		cancelable_result_base &operator= (cancelable_result_base other) {
			swap(*this, other);
			return *this;
		}

		cancelable_awaitable<T> *get_awaitable() {
			return object;
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

template<typename T>
struct cancelable_result : private detail::cancelable_result_base<T> {
private:
	using detail::cancelable_result_base<T>::object;

public:
	cancelable_result() = default;

	explicit cancelable_result(cancelable_awaitable<T> *obj)
	: detail::cancelable_result_base<T>{obj} { }
	
	operator result<T> () && {
		return result<T>{std::exchange(object, nullptr)};
	}

	void then(callback<void(T)> awaiter) {
		assert(object);
		object->then(awaiter);
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

	explicit cancelable_result(cancelable_awaitable<void> *obj)
	: detail::cancelable_result_base<void>{obj} { }
	
	operator result<void> () && {
		return result<void>{std::exchange(object, nullptr)};
	}

	void then(callback<void()> awaiter) {
		assert(object);
		object->then(awaiter);
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

template<typename T>
async::detail::result_awaiter<T> cofiber_awaiter(async::cancelable_result<T> res) {
	return {async::result<T>{std::move(res)}};
};

template<typename T>
struct complete_or_cancel {
	complete_or_cancel(result_reference<T> complete_future, result_reference<void> cancel_future)
	: _complete_awaitable{complete_future.get_awaitable()},
			_cancel_awaitable{cancel_future.get_awaitable()},
			_wait_complete{false}, _wait_cancel{false} {
		assert(_complete_awaitable);
	}

	bool await_ready() {
		return _complete_awaitable->ready()
				|| (_cancel_awaitable && _cancel_awaitable->ready());
	}

	void await_suspend(cofiber::coroutine_handle<> handle) {
		_handle = handle;
		
		_wait_complete = true;
		if(_cancel_awaitable)
			_wait_cancel = true;

		assert(!_complete_awaitable->ready());
		_complete_awaitable->then([this] {
			assert(_wait_complete);
			assert(_complete_awaitable->ready());
			_wait_complete = false;

			// Interrupt the other callback.
			if(_cancel_awaitable && _cancel_awaitable->interrupt())
				_wait_cancel = false;

			// Only resume after both callbacks are disarmed.
			if(!_wait_cancel)
				_handle.resume();
		});	
	
		if(_cancel_awaitable) {
			assert(!_cancel_awaitable->ready());
			_cancel_awaitable->then([this] {
				assert(_wait_cancel);
				assert(_cancel_awaitable->ready());
				_wait_cancel = false;

				// Interrupt the other callback.
				if(_complete_awaitable->interrupt())
					_wait_complete = false;

				// Only resume after both callbacks are disarmed.
				if(!_wait_complete)
					_handle.resume();
			});
		}
	}

	bool await_resume() {
		assert(!_wait_complete && !_wait_cancel);
		assert(_complete_awaitable->ready()
				|| (_cancel_awaitable && _cancel_awaitable->ready()));
		return _complete_awaitable->ready();
	}

private:
	awaitable<T> *_complete_awaitable;
	awaitable<void> *_cancel_awaitable;
	bool _wait_complete;
	bool _wait_cancel;
	cofiber::coroutine_handle<> _handle;
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

namespace async {
	struct want_cancel_future_t { };

	inline constexpr want_cancel_future_t want_cancel_future;
}

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
	
	template<typename T>
	struct coroutine_traits<async::cancelable_result<T>> {
		struct promise_type : private async::cancelable_awaitable<T> {
			promise_type() { }

		private:
			void dispose() override {
				auto handle = coroutine_handle<promise_type>::from_promise(*this);
				handle.destroy();
			}

		public:
			async::cancelable_result<T> get_return_object(coroutine_handle<>) {
				return async::cancelable_result<T>{this};
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

		private:
			struct yield_awaiter {
				yield_awaiter(promise_type *p)
				: _p{p} { }

				bool await_ready() {
					return true;
				}

				void await_suspend(cofiber::coroutine_handle<>) {
						std::cerr << "libasync: Internal fatal error:"
								" Coroutine suspened in yield_awaiter." << std::endl;
						std::terminate();
				}

				async::result<void> await_resume() {
					return _p->_cp.async_get();
				}
			
			private:
				promise_type *_p;
			};

		public:
			yield_awaiter yield_value(async::want_cancel_future_t) {
				return yield_awaiter{this};
			}

			void cancel() override {
				_cp.set_value();
			}

		private:
			async::promise<void> _cp;
		};
	};

/*
	template<typename T>
	struct coroutine_traits<async::cancelable_result<T>> {
		struct promise_type
		: private smarter::counter,
				private async::cancelable_awaitable<T> {
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
				yield_awaiter(promise_type *p, async::result<X> res)
				: _p{p}, _res{std::move(res)} {
					_st = new cancel_state<X>;

					_res.then([this] {
						_st->result = std::move(_res.value());
						_st->ready = true;
						if(_st->waiting)
							_st->handle.resume();
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
				async::result<X> _res;
			};

			promise_type()
			: _yield_state{nullptr} {
				setup(smarter::adopt_rc, nullptr, 2);
			}

		private:
			void dispose() override {
				std::cout << "libasync: Handle dispose() for cancelable_result<void>"
						" promise_type" << std::endl;
			}

		public:
			async::cancelable_result<T> get_return_object(coroutine_handle<> handle) {
				smarter::shared_ptr<async::cancelable_awaitable<T>> ptr{smarter::adopt_rc,
						this, this};
				return async::cancelable_result<T>{std::move(ptr)};
			}

			// TODO: We need to suspend_always on final suspend to get the lifetime
			// of cancel() correct!
			auto initial_suspend() { return suspend_never(); }
			auto final_suspend() { return suspend_always(); }

			template<typename... V>
			void return_value(V &&... value) {
				async::cancelable_awaitable<T>::emplace_value(std::forward<V>(value)...);
				async::cancelable_awaitable<T>::set_ready();
			}

			template<typename X>
			yield_awaiter<X> yield_value(async::result<X> result) {
				return yield_awaiter<X>{this, std::move(result)};
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
			cancel_state_base *_yield_state;
		};
	};
	
	template<>
	struct coroutine_traits<async::cancelable_result<void>> {
		struct promise_type
		: private smarter::counter,
				private async::cancelable_awaitable<void> {
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
				yield_awaiter(promise_type *p, async::result<X> res)
				: _p{p}, _res{std::move(res)} {
					_st = new cancel_state<X>;

					_res.then([this] () {
						_st->result = std::move(_res.value());
						_st->ready = true;
						if(_st->waiting)
							_st->handle.resume();
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
				async::result<X> _res;
			};

			promise_type()
			: _yield_state{nullptr} {
				setup(smarter::adopt_rc, nullptr, 2);
			}

		private:
			void dispose() override {
				std::cout << "libasync: Handle dispose() for cancelable_result<void>"
						" promise_type" << std::endl;
			}

		public:
			async::cancelable_result<void> get_return_object(coroutine_handle<> handle) {
				smarter::shared_ptr<async::cancelable_awaitable<void>> ptr{smarter::adopt_rc,
						this, this};
				return async::cancelable_result<void>{std::move(ptr)};
			}

			// TODO: We need to suspend_always on final suspend to get the lifetime
			// of cancel() correct!
			auto initial_suspend() { return suspend_never(); }
			auto final_suspend() { return suspend_always(); }

			void return_value() {
				set_ready();
			}

			template<typename X>
			yield_awaiter<X> yield_value(async::result<X> result) {
				return yield_awaiter<X>{this, std::move(result)};
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
			cancel_state_base *_yield_state;
		};
	};
*/
}

#endif // ASYNC_RESULT_HPP
