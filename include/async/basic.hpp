#ifndef LIBASYNC_BASIC_HPP
#define LIBASYNC_BASIC_HPP

#include <mutex>
#include <optional>

#include <async/execution.hpp>
#include <boost/intrusive/list.hpp>
#include <frg/optional.hpp>

namespace async {

// ----------------------------------------------------------------------------
// sender_awaiter template.
// ----------------------------------------------------------------------------

template<typename S, typename T = void>
struct [[nodiscard]] sender_awaiter {
private:
	struct receiver {
		void set_value(T result) {
			p_->result_ = std::move(result);
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
// run_queue implementation.
// ----------------------------------------------------------------------------

struct io_service;
struct run_queue;

run_queue *get_current_queue();

struct run_queue_item {
	friend struct run_queue;

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
	boost::intrusive::list_member_hook<> _hook;
};

struct io_service {
	virtual ~io_service() = default;

	virtual void wait() = 0;
};

struct run_queue {
	run_queue(io_service *io_svc)
	: _io_svc{io_svc} { }

public:
	void post(run_queue_item *node);

	void run();

private:
	io_service *_io_svc;

	boost::intrusive::list<
		run_queue_item,
		boost::intrusive::member_hook<
			run_queue_item,
			boost::intrusive::list_member_hook<>,
			&run_queue_item::_hook
		>
	> _run_list;
};

struct queue_scope {
	queue_scope(run_queue *queue);

	queue_scope(const queue_scope &) = delete;

	~queue_scope();

	queue_scope &operator= (const queue_scope &) = delete;

private:
	run_queue *_queue;
};

inline void run_queue::post(run_queue_item *item) {
	// TODO: Implement cross-queue posting.
	assert(get_current_queue() == this);
	assert(item->_cb && "run_queue_item is posted with a null callback");

	_run_list.push_back(*item);
}

inline void run_queue::run() {
	queue_scope scope{this};

	while(true) {
		while(!_run_list.empty()) {
			auto item = &_run_list.front();
			_run_list.pop_front();
			item->_cb();
		}

		_io_svc->wait();
	}
}

// ----------------------------------------------------------------------------
// queue_scope implementation.
// ----------------------------------------------------------------------------

inline thread_local run_queue *_thread_current_queue{nullptr};

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
				std::cerr << "libasync: Internal fatal error:"
						" Coroutine resumed from final suspension point" << std::endl;
				std::terminate();
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
			std::cerr << "libasync: Unhandled exception in coroutine" << std::endl;
			std::terminate();
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
	std::optional<T> _val;
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
