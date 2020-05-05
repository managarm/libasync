#ifndef LIBASYNC_BASIC_HPP
#define LIBASYNC_BASIC_HPP

#include <atomic>
#include <experimental/coroutine>

#include <async/execution.hpp>
#include <frg/list.hpp>
#include <frg/optional.hpp>
#include <frg/mutex.hpp>

#ifndef LIBASYNC_CUSTOM_PLATFORM
#include <mutex>
#include <iostream>
#include <cassert>
#define LIBASYNC_THREAD_LOCAL thread_local
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

namespace async {

// ----------------------------------------------------------------------------
// sender_awaiter template.
// ----------------------------------------------------------------------------

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

	void await_suspend(std::experimental::coroutine_handle<> h) {
		h_ = h;
		execution::start(operation_);
	}

	T await_resume() {
		return std::move(*result_);
	}

	execution::operation_t<S, receiver> operation_;
	std::experimental::coroutine_handle<> h_;
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

	void await_suspend(std::experimental::coroutine_handle<> h) {
		h_ = h;
		execution::start(operation_);
	}

	void await_resume() {
		// Do nothing.
	}

	execution::operation_t<S, receiver> operation_;
	std::experimental::coroutine_handle<> h_;
};

// ----------------------------------------------------------------------------
// Legacy utilities.
// ----------------------------------------------------------------------------

template<typename S>
struct callback;

template<typename R, typename... Args>
struct callback<R(Args...)> {
private:
	using storage = std::aligned_storage_t<sizeof(void *), alignof(void *)>;

	template<typename F>
	static R invoke(storage object, Args... args) {
		return (*reinterpret_cast<F *>(&object))(std::move(args)...);
	}

public:
	callback()
	: _function(nullptr) { }

	template<typename F, typename = std::enable_if_t<
			sizeof(F) == sizeof(void *) && alignof(F) == alignof(void *)
			&& std::is_trivially_copy_constructible<F>::value
			&& std::is_trivially_destructible<F>::value>>
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
	std::aligned_storage_t<sizeof(void *), alignof(void *)> _object;
};

// ----------------------------------------------------------------------------
// Top-level execution functions.
// ----------------------------------------------------------------------------

template<typename RunToken, typename IoService>
void run_forever(RunToken rt, IoService ios) {
	while(true) {
		rt.run_iteration();
		if(!rt.is_drained())
			continue;
		ios.wait();
	}
}

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

template<typename Sender, typename RunToken>
std::enable_if_t<std::is_same_v<typename Sender::value_type, void>, void>
run(Sender s, RunToken rt) {
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
	execution::start(operation);

	while(!st.done) {
		assert(!rt.is_drained());
		rt.run_iteration();
	}
}

template<typename Sender, typename RunToken>
std::enable_if_t<!std::is_same_v<typename Sender::value_type, void>,
		typename Sender::value_type>
run(Sender s, RunToken rt) {
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
	execution::start(operation);

	while(!st.done) {
		assert(!rt.is_drained());
		rt.run_iteration();
	}
	return std::move(*st.value);
}

struct queue_scope {
	queue_scope(run_queue *queue);

	queue_scope(const queue_scope &) = delete;

	~queue_scope();

	queue_scope &operator= (const queue_scope &) = delete;

private:
	run_queue *_queue;
};

struct current_queue_token {
	void run_iteration();
	bool is_drained();
};

inline constexpr current_queue_token current_queue;

inline void run_queue::post(run_queue_item *item) {
	// TODO: Implement cross-queue posting.
	assert(get_current_queue() == this);
	assert(item->_cb && "run_queue_item is posted with a null callback");

	_run_list.push_back(item);
}

// ----------------------------------------------------------------------------
// queue_scope implementation.
// ----------------------------------------------------------------------------

inline LIBASYNC_THREAD_LOCAL run_queue *_thread_current_queue{nullptr};

inline run_queue *get_current_queue() {
	return _thread_current_queue;
}

inline queue_scope::queue_scope(run_queue *queue)
: _queue{queue} {
	_thread_current_queue = _queue;
}

inline queue_scope::~queue_scope() {
	assert(_thread_current_queue == _queue);
	_thread_current_queue = nullptr;
}

// ----------------------------------------------------------------------------
// queue_token implementation.
// ----------------------------------------------------------------------------

inline bool run_queue_token::is_drained() {
	return rq_->_run_list.empty();
}

inline void run_queue_token::run_iteration() {
	queue_scope rqs{rq_};

	while(!rq_->_run_list.empty()) {
		auto item = rq_->_run_list.front();
		rq_->_run_list.pop_front();
		item->_cb();
	}
}

inline bool current_queue_token::is_drained() {
	auto rq = get_current_queue();
	assert(rq && "current_queue_token is used outside of queue");
	return rq->_run_list.empty();
}

inline void current_queue_token::run_iteration() {
	auto rq = get_current_queue();
	assert(rq && "current_queue_token is used outside of queue");

	while(!rq->_run_list.empty()) {
		auto item = rq->_run_list.front();
		rq->_run_list.pop_front();
		item->_cb();
	}
}

// ----------------------------------------------------------------------------
// Utilities related to run_queues.
// ----------------------------------------------------------------------------

struct resumption_on_current_queue {
	struct token {
		token() = default;

		void arm(const resumption_on_current_queue &, callback<void()> cb) {
			_rqi.arm(cb);
		}

		void post(const resumption_on_current_queue &) {
			auto q = get_current_queue();
			assert(q && "resumption_on_current_queue token is posted outside of queue");
			q->post(&_rqi);
		}

	private:
		run_queue_item _rqi;
	};
};

struct yield_sender {
	run_queue *q;
};

inline yield_sender yield_to_current_queue() {
	auto q = get_current_queue();
	assert(q && "yield_to_current_queue() outside of queue");
	return yield_sender{q};
}

template<typename Receiver>
struct yield_operation {
	yield_operation(yield_sender s, Receiver r)
	: q_{s.q}, r_{std::move(r)} {
		_rqi.arm([this] {
			r_.set_value();
		});
	}

	void start() {
		q_->post(&_rqi);
	}

private:
	run_queue *q_;
	Receiver r_;
	run_queue_item _rqi;
};

template<typename Receiver>
yield_operation<Receiver> connect(yield_sender s, Receiver r) {
	return {s, std::move(r)};
}

inline async::sender_awaiter<yield_sender, void>
operator co_await(yield_sender s) {
	return {s};
};

// ----------------------------------------------------------------------------
// Detached coroutines.
// ----------------------------------------------------------------------------

struct detached {
	struct promise_type {
		struct initial_awaiter {
			bool await_ready() {
				return false;
			}

			void await_suspend(std::experimental::coroutine_handle<> h) {
				_rt.arm(_rm, [address = h.address()] {
					auto h = std::experimental::coroutine_handle<>::from_address(address);
					h.resume();
				});
				_rt.post(_rm);
			}

			void await_resume() { }

		private:
			resumption_on_current_queue _rm;
			resumption_on_current_queue::token _rt;
		};

		struct final_awaiter {
			bool await_ready() {
				return false;
			}

			void await_suspend(std::experimental::coroutine_handle<> h) {
				// Calling h.destroy() here causes the code to break.
				// TODO: Is this a LLVM bug? Workaround: Defer it to a run_queue.
				_rt.arm(_rm, [address = h.address()] {
					auto h = std::experimental::coroutine_handle<>::from_address(address);
					h.destroy();
				});
				_rt.post(_rm);
			}

			void await_resume() {
				platform::panic("libasync: Internal fatal error:"
						" Coroutine resumed from final suspension point");
			}

		private:
			resumption_on_current_queue _rm;
			resumption_on_current_queue::token _rt;
		};

		detached get_return_object() {
			return {};
		}

		initial_awaiter initial_suspend() {
			return {};
		}

		final_awaiter final_suspend() {
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

template<typename A>
detached detach(A awaitable) {
	return detach(std::move(awaitable), [] { });
}

// TODO: Use a specialized coroutine promise that allows us to control
//       the run_queue that the coroutine is executed on.
template<typename A, typename Cont>
detached detach(A awaitable, Cont continuation) {
	co_await std::move(awaitable);
	continuation();
}

// ----------------------------------------------------------------------------
// awaitable.
// ----------------------------------------------------------------------------

struct awaitable_base {
	friend struct run_queue;

private:
	static constexpr int consumer_alive = 1;
	static constexpr int producer_alive = 2;

	enum class ready_state {
		null, ready, retired
	};

public:
	awaitable_base();

	awaitable_base(const awaitable_base &) = delete;

	awaitable_base &operator= (const awaitable_base &) = delete;

	bool ready() {
		return _ready.load(std::memory_order_acquire) != ready_state::null;
	}

	void then(callback<void()> cb) {
		assert(_ready.load(std::memory_order_relaxed) != ready_state::retired);
		_cb = cb;
		_rt.arm(_rm, [this] { _retire(); });
		submit();
	}

	void drop() {
		dispose();
	}

protected:
	void set_ready();

	virtual void submit() = 0;

	virtual void dispose() = 0;

private:
	void _retire() {
		// TODO: Do we actually need release semantics here?
		assert(_ready.load(std::memory_order_relaxed) == ready_state::ready);
		_ready.store(ready_state::retired, std::memory_order_release);
		assert(_cb);
		_cb();
	}

private:
	std::atomic<ready_state> _ready;
	callback<void()> _cb;
	resumption_on_current_queue _rm;
	resumption_on_current_queue::token _rt;
};

inline awaitable_base::awaitable_base()
: _ready{ready_state::null} { }

inline void awaitable_base::set_ready() {
	assert(_ready.load(std::memory_order_relaxed) == ready_state::null);
	_ready.store(ready_state::ready, std::memory_order_release);
	_rt.post(_rm);
}

template<typename T>
struct awaitable : awaitable_base {
	virtual ~awaitable() { }

	T &value() {
		return _val.value();
	}

protected:
	template<typename... Args>
	void emplace_value(Args &&... args) {
		_val.emplace(std::forward<Args>(args)...);
	}

private:
	frg::optional<T> _val;
};

template<>
struct awaitable<void> : awaitable_base {
	virtual ~awaitable() { }

protected:
	void emplace_value() { }
};

template<typename T>
struct cancelable_awaitable : awaitable<T> {
	virtual void cancel() = 0;
};

} // namespace async

#endif // LIBASYNC_BASIC_HPP
