#pragma once

#include <atomic>
#include <concepts>
#include <type_traits>

#include <async/execution.hpp>
#include <async/platform-support.hpp>
#include <async/run-queue.hpp>
#include <frg/list.hpp>
#include <frg/optional.hpp>
#include <frg/mutex.hpp>
#include <frg/eternal.hpp>
#include <frg/std_compat.hpp>

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

#ifndef LIBASYNC_CUSTOM_PLATFORM

// ----------------------------------------------------------------------------
// Queue-affine awaiting of senders.
// ----------------------------------------------------------------------------

// Awaiter that resumes the coroutine on its run queue instead of resuming it inline from set_value().
template<Sender S>
struct queue_affine_awaiter : run_queue_item {
	struct env {
		run_queue *get_run_queue() {
			return aw->rq_;
		}

		queue_affine_awaiter *aw;
	};

	struct receiver {
		template<typename... Args>
		void set_value(Args &&... args) {
			aw->value_.emplace(std::forward<Args>(args)...);
			if(aw->rq_) {
				aw->setup([] (run_queue_item *base) {
					auto aw = static_cast<queue_affine_awaiter *>(base);
					aw->h_.resume();
				});
				aw->rq_->post(aw);
			}else{
				// If the coroutine has no run queue, resume inline.
				aw->h_.resume();
			}
		}

		auto get_env() {
			return env{.aw = aw};
		}

		queue_affine_awaiter *aw;
	};

	queue_affine_awaiter(S s, run_queue *rq)
	: op_{execution::connect(std::move(s), receiver{this})}, rq_{rq} { }

	bool await_ready() {
		return false;
	}

	void await_suspend(corons::coroutine_handle<> h) {
		h_ = h;
		execution::start(op_);
	}

	typename S::value_type await_resume() {
		assert(value_);
		if constexpr (!std::is_same_v<typename S::value_type, void>)
			return std::move(*value_);
	}

private:
	struct empty { };

	execution::operation_t<S, receiver> op_;
	run_queue *rq_;
	corons::coroutine_handle<> h_;

	std::optional<
		std::conditional_t<
			std::is_same_v<typename S::value_type, void>,
			empty,
			typename S::value_type
		>
	> value_;
};

#endif // LIBASYNC_CUSTOM_PLATFORM

// ----------------------------------------------------------------------------
// Top-level execution functions.
// ----------------------------------------------------------------------------

// TODO: It makes more sense to demand a run() method.
template<typename T>
concept Waitable = requires (T t) {
	t.wait();
};

struct dummy_io_service {
	void wait() {
		// TODO: dummy_io_service could use a futex to wait.
		platform::panic("dummy_io_service does not know how to wait");
	}
};
static_assert(Waitable<dummy_io_service>);

template<Sender Sender, Waitable IoService>
requires std::same_as<typename Sender::value_type, void>
void run(Sender s, IoService ios) {
	struct state {
		bool done = false;
		run_queue *rq = nullptr;
	};

	struct env {
		run_queue *get_run_queue() {
			return stp_->rq;
		}

		state *stp_;
	};

	struct receiver {
		receiver(state *stp)
		: stp_{stp} { }

		void set_value() {
			stp_->done = true;
		}

		auto get_env() {
			return env{stp_};
		}

	private:
		state *stp_;
	};

	state st;
	if constexpr (has_get_run_queue<IoService>)
		st.rq = ios.get_run_queue();

	auto operation = execution::connect(std::move(s), receiver{&st});
	execution::start(operation);

	while(!st.done) {
		ios.wait();
	}
}

template<Sender Sender, typename IoService>
requires (!std::same_as<typename Sender::value_type, void>)
typename Sender::value_type run(Sender s, IoService ios) {
	struct state {
		bool done = false;
		run_queue *rq = nullptr;
		frg::optional<typename Sender::value_type> value;
	};

	struct env {
		run_queue *get_run_queue() {
			return stp_->rq;
		}

		state *stp_;
	};

	struct receiver {
		receiver(state *stp)
		: stp_{stp} { }

		void set_value(typename Sender::value_type value) {
			stp_->value.emplace(std::move(value));
			stp_->done = true;
		}

		auto get_env() {
			return env{stp_};
		}

	private:
		state *stp_;
	};

	state st;
	if constexpr (has_get_run_queue<IoService>)
		st.rq = ios.get_run_queue();

	auto operation = execution::connect(std::move(s), receiver{&st});
	execution::start(operation);

	while(!st.done) {
		ios.wait();
	}

	return std::move(*st.value);
}

template<Sender Sender>
auto run(Sender s) {
	return run(std::move(s), dummy_io_service{});
}

template<Receives<void> R>
struct forever_operation {
	void start() {
		// Do nothing.
	}

	R receiver;
};

struct forever_sender {
	using value_type = void;

	template<Receives<void> R>
	forever_operation<R> connect(R &&receiver) {
		return {std::move(receiver)};
	}
};
static_assert(Sender<forever_sender>);

template<Waitable IoService>
void run_forever(IoService ios) {
	return run(forever_sender{}, std::move(ios));
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

#ifndef LIBASYNC_CUSTOM_PLATFORM
		template<typename S>
		requires Sender<std::remove_cvref_t<S>>
		auto await_transform(S &&s) {
			return queue_affine_awaiter<std::remove_cvref_t<S>>{std::forward<S>(s), rq_};
		}

		template<typename A>
		requires (!Sender<std::remove_cvref_t<A>>)
		decltype(auto) await_transform(A &&a) {
			return std::forward<A>(a);
		}

	private:
		run_queue *rq_ = get_default_queue();
#endif // LIBASYNC_CUSTOM_PLATFORM
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

		struct env {
			run_queue *get_run_queue() {
				return rq_;
			}

			run_queue *rq_;
		};

		auto get_env() {
			return env{cb_->rq};
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

		control_block(Allocator allocator, run_queue *rq, S sender, Cont continuation)
		: allocator{std::move(allocator)}, rq{rq},
				operation{execution::connect(
						std::move(sender), final_receiver<Allocator, S, Cont>{this})},
				continuation{std::move(continuation)} { }

		Allocator allocator;
		run_queue *rq;
		execution::operation_t<S, final_receiver<Allocator, S, Cont>> operation;
		Cont continuation;
	};
}

template<typename Allocator, typename S, typename Cont>
void detach_with_allocator_on(run_queue *rq, Allocator allocator, S sender, Cont continuation) {
	auto p = frg::construct<detach_details_::control_block<Allocator, S, Cont>>(allocator,
			allocator, rq, std::move(sender), std::move(continuation));
	execution::start_inline(p->operation);
}

template<typename Allocator, typename S, typename Cont>
void detach_with_allocator(Allocator allocator, S sender, Cont continuation) {
	detach_with_allocator_on<Allocator, S, Cont>(get_default_queue(), std::move(allocator),
			std::move(sender), std::move(continuation));
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

template<Sender S>
void detach_on(run_queue *rq, S sender) {
	return detach_with_allocator_on(rq, frg::stl_allocator{}, std::move(sender), [] { });
}

template<Sender S, typename Cont>
void detach_on(run_queue *rq, S sender, Cont continuation) {
	return detach_with_allocator_on(rq, frg::stl_allocator{}, std::move(sender),
			std::move(continuation));
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

		struct env {
			run_queue *get_run_queue() {
				return rq_;
			}

			run_queue *rq_;
		};

		auto get_env() {
			return env{cb_->rq};
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

		control_block(Allocator allocator, run_queue *rq, S sender, R dr)
		: allocator{std::move(allocator)}, rq{rq},
				operation{execution::connect(
						std::move(sender), final_receiver<Allocator, S, R>{this})},
				dr{std::move(dr)} { }

		Allocator allocator;
		run_queue *rq;
		execution::operation_t<S, final_receiver<Allocator, S, R>> operation;
		R dr; // Downstream receiver.
	};
}

template<typename Allocator, typename S, typename R>
void spawn_with_allocator_on(run_queue *rq, Allocator allocator, S sender, R receiver) {
	auto p = frg::construct<spawn_details_::control_block<Allocator, S, R>>(allocator,
			allocator, rq, std::move(sender), std::move(receiver));
	execution::start_inline(p->operation);
}

template<typename Allocator, typename S, typename R>
void spawn_with_allocator(Allocator allocator, S sender, R receiver) {
	spawn_with_allocator_on<Allocator, S, R>(get_default_queue(), std::move(allocator),
			std::move(sender), std::move(receiver));
}

template<Sender S, typename R>
void spawn_on(run_queue *rq, S sender, R receiver) {
	return spawn_with_allocator_on(rq, frg::stl_allocator{}, std::move(sender),
			std::move(receiver));
}

} // namespace async
