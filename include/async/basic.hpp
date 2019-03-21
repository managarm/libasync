#ifndef LIBASYNC_BASIC_HPP
#define LIBASYNC_BASIC_HPP

#include <mutex>
#include <optional>

#include <boost/intrusive/list.hpp>
#include <cofiber.hpp>

namespace async {

namespace detail {
	// TODO: Use a specialized coroutine promise that allows us to control
	//       the run_queue that the coroutine is executed on.
	template<typename A, typename Cont>
	COFIBER_ROUTINE(cofiber::no_future, do_await(A awaitable, Cont continuation),
			([aw = std::move(awaitable), ct = std::move(continuation)] () mutable {
		COFIBER_AWAIT std::move(aw);
		ct();
	}));
}

template<typename A>
void detach(A awaitable) {
	detach(std::move(awaitable), [] { });
}

template<typename A, typename Cont>
void detach(A awaitable, Cont continuation) {
	do_await(std::move(awaitable), std::move(continuation));
}

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
