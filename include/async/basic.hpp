#pragma once

#include <atomic>
#include <concepts>
#include <type_traits>

#include <async/execution.hpp>
#include <frg/list.hpp>
#include <frg/optional.hpp>
#include <frg/mutex.hpp>
#include <frg/eternal.hpp>
#include <frg/std_compat.hpp>

#ifndef LIBASYNC_CUSTOM_PLATFORM
#include <mutex>
#include <iostream>
#include <cassert>

namespace async::platform {
	using mutex = std::mutex;

	[[noreturn]] inline void panic(const char *str) {
		std::cerr << str << std::endl;
		std::terminate();
	}
} // namespace async::platform
#else
#include <async/platform.hpp>
#endif

#if __has_include(<coroutine>) && !defined(LIBASYNC_FORCE_USE_EXPERIMENTAL)
#include <coroutine>
namespace corons = std;
#else
#include <experimental/coroutine>
namespace corons = std::experimental;
#endif

namespace async {
template<typename T, typename Value>
concept Receives = std::movable<T>
&& (std::same_as<Value, void> ?
(requires(T t) {
	{ t.set_value() } -> std::same_as<void>;
} || requires(T t) {
	{ t.set_value_noinline() } -> std::same_as<void>;
})
: (requires(T t) {
	{ t.set_value(std::declval<Value>()) } -> std::same_as<void>;
}) || requires(T t) {
	{ t.set_value_noinline(std::declval<Value>()) } -> std::same_as<void>;
});

namespace helpers {
template<auto>
struct dummy_receiver {
	template<typename T>
	requires (!std::same_as<T, void>)
	void set_value(T) {
		assert(std::is_constant_evaluated());
	}
	void set_value() {
		assert(std::is_constant_evaluated());
	}
};
static_assert(Receives<dummy_receiver<[]{}>, void>);
static_assert(Receives<dummy_receiver<[]{}>, int>);
} /* namespace helpers */

template<typename T>
concept Operation = requires(T &t) {
	{ execution::start_inline(t) } -> std::same_as<bool>;
};

/* We require move constructible, rather than movable, since lambdas can be
 * move constructible but not movable
 */
template<typename T>
concept Sender = std::move_constructible<T> && requires(T t) {
	typename T::value_type;
	{ execution::connect(std::move(t), helpers::dummy_receiver<[]{}>{}) }
		-> Operation;
};

template<typename E>
requires requires(E &&e) { operator co_await(std::forward<E>(e)); }
auto make_awaiter(E &&e) {
	return operator co_await(std::forward<E>(e));
}

template<typename E>
requires requires(E &&e) { std::forward<E>(e).operator co_await(); }
auto make_awaiter(E &&e) {
	return std::forward<E>(e).operator co_await();
}

template <typename Awaitable, typename T>
concept co_awaits_to = requires (Awaitable &&a) {
	{ make_awaiter(std::forward<Awaitable>(a)).await_resume() } -> std::same_as<T>;
};

enum class maybe_cancelled {
    not_cancelled,
    cancelled,
};

enum class maybe_awaited {
    awaited,
    condition_failed,
};

// ----------------------------------------------------------------------------
// sender_awaiter template.
// ----------------------------------------------------------------------------

/* we can't declare S a sender here, since, if we do, it'd be impossible to
 * declare a member co_await that returns a sender_awaiter
 */
template<typename S, typename T = void>
struct [[nodiscard]] sender_awaiter {
private:
	struct receiver {
		void set_value(T result) {
			p_->result_.emplace(std::move(result));
			p_->h_.resume();
		}

		sender_awaiter *p_;
	};

public:
	sender_awaiter(S sender)
	: operation_{execution::connect(std::move(sender), receiver{this})} {
	}

	bool await_ready() {
		return false;
	}

	bool await_suspend(corons::coroutine_handle<> h) {
		h_ = h;
		return !execution::start_inline(operation_);
	}

	T await_resume() {
		return std::move(*result_);
	}

	execution::operation_t<S, receiver> operation_;
	corons::coroutine_handle<> h_;
	frg::optional<T> result_;
};

// Specialization of sender_awaiter for void return types.
template<typename S>
struct [[nodiscard]] sender_awaiter<S, void> {
private:
	struct receiver {
		void set_value() {
			p_->h_.resume();
		}

		sender_awaiter *p_;
	};

public:
	sender_awaiter(S sender)
	: operation_{execution::connect(std::move(sender), receiver{this})} {
	}

	bool await_ready() {
		return false;
	}

	bool await_suspend(corons::coroutine_handle<> h) {
		h_ = h;
		return !execution::start_inline(operation_);
	}

	void await_resume() {
		// Do nothing.
	}

	execution::operation_t<S, receiver> operation_;
	corons::coroutine_handle<> h_;
};

// ----------------------------------------------------------------------------
// any_receiver<T>.
// ----------------------------------------------------------------------------

// This form of any_receiver is a broken concept: because it directly forwards
// the value of the set_value() function, it requires a virtual call even
// if we add an inline return path.

template<typename T>
struct any_receiver {
	template<typename R>
	requires (
	   std::is_trivially_copyable_v<R>
	&& sizeof(R) <= sizeof(void *)
	&& alignof(R) <= alignof(void *)
	)
	any_receiver(R receiver) {
		new (stor_) R(receiver);
		set_value_fptr_ = [] (void *p, T value) {
			auto *rp = static_cast<R *>(p);
			execution::set_value(*rp, std::move(value));
		};
	}

	void set_value(T value) {
		set_value_fptr_(stor_, std::move(value));
	}

private:
	alignas(alignof(void *)) char stor_[sizeof(void *)];
	void (*set_value_fptr_) (void *, T);
};

template<>
struct any_receiver<void> {
	template<typename R>
	requires (
	   std::is_trivially_copyable_v<R>
	&& sizeof(R) <= sizeof(void *)
	&& alignof(R) <= alignof(void *)
	)
	any_receiver(R receiver) {
		new (stor_) R(receiver);
		set_value_fptr_ = [] (void *p) {
			auto *rp = static_cast<R *>(p);
			execution::set_value(*rp);
		};
	}

	void set_value() {
		set_value_fptr_(stor_);
	}

private:
	alignas(alignof(void *)) char stor_[sizeof(void *)];
	void (*set_value_fptr_) (void *);
};

// ----------------------------------------------------------------------------
// Legacy utilities.
// ----------------------------------------------------------------------------

template<typename S>
struct callback;

template<typename R, typename... Args>
struct callback<R(Args...)> {
private:
	using storage = frg::aligned_storage<sizeof(void *), alignof(void *)>;

	template<typename F>
	static R invoke(storage object, Args... args) {
		return (*reinterpret_cast<F *>(&object))(std::move(args)...);
	}

public:
	callback()
	: _function(nullptr) { }

	template<typename F>
	requires (
	   sizeof(F) <= sizeof(void*)
	&& alignof(F) <= alignof(void*)
	&& std::is_trivially_copy_constructible_v<F>
	&& std::is_trivially_destructible_v<F>
	)
	callback(F functor)
	: _function(&invoke<F>) {
		new (&_object) F{std::move(functor)};
	}

	explicit operator bool () {
		return static_cast<bool>(_function);
	}

	R operator() (Args... args) {
		return _function(_object, std::move(args)...);
	}

private:
	R (*_function)(storage, Args...);
	frg::aligned_storage<sizeof(void *), alignof(void *)> _object;
};

// ----------------------------------------------------------------------------
// run_queue implementation.
// ----------------------------------------------------------------------------

struct run_queue;

run_queue *get_current_queue();

struct run_queue_item {
	friend struct run_queue;
	friend struct current_queue_token;
	friend struct run_queue_token;

	run_queue_item() = default;

	run_queue_item(const run_queue_item &) = delete;

	run_queue_item &operator= (const run_queue_item &) = delete;

	void arm(callback<void()> cb) {
		assert(!_cb && "run_queue_item is already armed");
		assert(cb && "cannot arm run_queue_item with a null callback");
		_cb = cb;
	}

private:
	callback<void()> _cb;
	frg::default_list_hook<run_queue_item> _hook;
};

struct run_queue_token {
	run_queue_token(run_queue *rq)
	: rq_{rq} { }

	void run_iteration();
	bool is_drained();

private:
	run_queue *rq_;
};

struct run_queue {
	friend struct current_queue_token;
	friend struct run_queue_token;

	run_queue_token run_token() {
		return {this};
	}

	void post(run_queue_item *node);

private:
	frg::intrusive_list<
		run_queue_item,
		frg::locate_member<
			run_queue_item,
			frg::default_list_hook<run_queue_item>,
			&run_queue_item::_hook
		>
	> _run_list;
};

// ----------------------------------------------------------------------------
// Top-level execution functions.
// ----------------------------------------------------------------------------

template<typename T>
concept Waitable = requires (T t) {
	t.wait();
};

template<Waitable IoService>
void run_forever(IoService ios) {
	while(true) {
		ios.wait();
	}
}

template<Sender Sender>
requires std::same_as<typename Sender::value_type, void>
void run(Sender s) {
	struct receiver {
		void set_value() { }
	};

	auto operation = execution::connect(std::move(s), receiver{});
	if(execution::start_inline(operation))
		return;

	platform::panic("libasync: Operation hasn't completed and we don't know how to wait");
}

template<Sender Sender>
requires (!std::same_as<typename Sender::value_type, void>)
typename Sender::value_type run(Sender s) {
	struct state {
		frg::optional<typename Sender::value_type> value;
	};

	struct receiver {
		receiver(state *stp)
		: stp_{stp} { }

		void set_value(typename Sender::value_type value) {
			stp_->value.emplace(std::move(value));
		}

	private:
		state *stp_;
	};

	state st;

	auto operation = execution::connect(std::move(s), receiver{&st});
	if (execution::start_inline(operation))
		return std::move(*st.value);

	platform::panic("libasync: Operation hasn't completed and we don't know how to wait");
}

template<Sender Sender, Waitable IoService>
requires std::same_as<typename Sender::value_type, void>
void run(Sender s, IoService ios) {
	struct state {
		bool done = false;
	};

	struct receiver {
		receiver(state *stp)
		: stp_{stp} { }

		void set_value() {
			stp_->done = true;
		}

	private:
		state *stp_;
	};

	state st;

	auto operation = execution::connect(std::move(s), receiver{&st});
	if(execution::start_inline(operation))
		return;

	while(!st.done) {
		ios.wait();
	}
}

template<Sender Sender, typename IoService>
requires (!std::same_as<typename Sender::value_type, void>)
typename Sender::value_type run(Sender s, IoService ios) {
	struct state {
		bool done = false;
		frg::optional<typename Sender::value_type> value;
	};

	struct receiver {
		receiver(state *stp)
		: stp_{stp} { }

		void set_value(typename Sender::value_type value) {
			stp_->value.emplace(std::move(value));
			stp_->done = true;
		}

	private:
		state *stp_;
	};

	state st;

	auto operation = execution::connect(std::move(s), receiver{&st});
	if(execution::start_inline(operation))
		return std::move(*st.value);

	while(!st.done) {
		ios.wait();
	}

	return std::move(*st.value);
}

// ----------------------------------------------------------------------------
// Detached coroutines.
// ----------------------------------------------------------------------------

struct detached {
	struct promise_type {
		detached get_return_object() {
			return {};
		}

		corons::suspend_never initial_suspend() {
			return {};
		}

		corons::suspend_never final_suspend() noexcept {
			return {};
		}

		void return_void() {
			// Nothing to do here.
		}

		void unhandled_exception() {
			platform::panic("libasync: Unhandled exception in coroutine");
		}
	};
};

namespace detach_details_ {
	template<typename Allocator, typename S, typename Cont>
	struct control_block;

	template<typename Allocator, typename S, typename Cont>
	void finalize(control_block<Allocator, S, Cont> *cb);

	template<typename Allocator, typename S, typename Cont>
	struct final_receiver {
		final_receiver(control_block<Allocator, S, Cont> *cb)
		: cb_{cb} { }

		void set_value() {
			finalize(cb_);
		}

	private:
		control_block<Allocator, S, Cont> *cb_;
	};

	// Heap-allocate data structure that holds the operation.
	// We cannot directly put the operation onto the heap as it is non-movable.
	template<typename Allocator, typename S, typename Cont>
	struct control_block {
		friend void finalize(control_block<Allocator, S, Cont> *cb) {
			auto allocator = std::move(cb->allocator);
			auto continuation = std::move(cb->continuation);
			frg::destruct(allocator, cb);
			continuation();
		}

		control_block(Allocator allocator, S sender, Cont continuation)
		: allocator{std::move(allocator)},
				operation{execution::connect(
						std::move(sender), final_receiver<Allocator, S, Cont>{this})},
				continuation{std::move(continuation)} { }

		Allocator allocator;
		execution::operation_t<S, final_receiver<Allocator, S, Cont>> operation;
		Cont continuation;
	};
}

template<typename Allocator, typename S, typename Cont>
void detach_with_allocator(Allocator allocator, S sender, Cont continuation) {
	auto p = frg::construct<detach_details_::control_block<Allocator, S, Cont>>(allocator,
			allocator, std::move(sender), std::move(continuation));
	execution::start_inline(p->operation);
}

template<typename Allocator, typename S>
void detach_with_allocator(Allocator allocator, S sender) {
	detach_with_allocator<Allocator, S>(std::move(allocator), std::move(sender), [] { });
}

template<Sender S>
void detach(S sender) {
	return detach_with_allocator(frg::stl_allocator{}, std::move(sender));
}

template<Sender S, typename Cont>
void detach(S sender, Cont continuation) {
	return detach_with_allocator(frg::stl_allocator{}, std::move(sender), std::move(continuation));
}

namespace spawn_details_ {
	template<typename Allocator, typename S, typename R>
	struct control_block;

	template<typename Allocator, typename S, typename R>
	void finalize(control_block<Allocator, S, R> *cb);

	template<typename Allocator, typename S, typename R>
	struct final_receiver {
		final_receiver(control_block<Allocator, S, R> *cb)
		: cb_{cb} { }

		template<typename... Args>
		void set_value(Args &&... args) {
			execution::set_value(cb_->dr, std::forward<Args>(args)...);
			finalize(cb_);
		}

	private:
		control_block<Allocator, S, R> *cb_;
	};

	// Heap-allocate data structure that holds the operation.
	// We cannot directly put the operation onto the heap as it is non-movable.
	template<typename Allocator, typename S, typename R>
	struct control_block {
		friend void finalize(control_block<Allocator, S, R> *cb) {
			auto allocator = std::move(cb->allocator);
			frg::destruct(allocator, cb);
		}

		control_block(Allocator allocator, S sender, R dr)
		: allocator{std::move(allocator)},
				operation{execution::connect(
						std::move(sender), final_receiver<Allocator, S, R>{this})},
				dr{std::move(dr)} { }

		Allocator allocator;
		execution::operation_t<S, final_receiver<Allocator, S, R>> operation;
		R dr; // Downstream receiver.
	};
}

template<typename Allocator, typename S, typename R>
void spawn_with_allocator(Allocator allocator, S sender, R receiver) {
	auto p = frg::construct<spawn_details_::control_block<Allocator, S, R>>(allocator,
			allocator, std::move(sender), std::move(receiver));
	execution::start_inline(p->operation);
}

} // namespace async
