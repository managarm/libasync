#pragma once

#include <array>
#include <optional>
#include <tuple>

#include <frg/list.hpp>
#include "basic.hpp"

namespace async::detail {

struct abstract_cancellation_callback {
	friend struct cancellation_event;

protected:
	virtual ~abstract_cancellation_callback() = default;

private:
	virtual void call() = 0;

	frg::default_list_hook<abstract_cancellation_callback> _hook;
};

struct cancellation_event {
	friend struct cancellation_token;

	template<typename F>
	friend struct cancellation_callback;

	template<typename F>
	friend struct cancellation_observer;

	template<typename TryCancel, typename Cont>
	friend struct cancellation_resolver;

	cancellation_event()
	: _was_requested{false} { };

	cancellation_event(const cancellation_event &) = delete;
	cancellation_event(cancellation_event &&) = delete;

	~cancellation_event() {
		assert(_cbs.empty() && "all callbacks must be destructed before"
				" cancellation_event is destructed");
	}

	cancellation_event &operator= (const cancellation_event &) = delete;
	cancellation_event &operator= (cancellation_event &&) = delete;

	void cancel();

	void reset();

private:
	platform::mutex _mutex;

	bool _was_requested;

	frg::intrusive_list<
		abstract_cancellation_callback,
		frg::locate_member<
			abstract_cancellation_callback,
			frg::default_list_hook<abstract_cancellation_callback>,
			&abstract_cancellation_callback::_hook
		>
	> _cbs;
};

struct cancellation_token {
	template<typename F>
	friend struct cancellation_callback;

	template<typename F>
	friend struct cancellation_observer;

	template<typename TryCancel, typename Cont>
	friend struct cancellation_resolver;

	cancellation_token()
	: _event{nullptr} { }

	cancellation_token(cancellation_event &event_ref)
	: _event{&event_ref} { }

	bool is_cancellation_requested() const {
		if(!_event)
			return false;
		frg::unique_lock guard{_event->_mutex};
		return _event->_was_requested;
	}

private:
	cancellation_event *_event;
};

template<typename F>
struct cancellation_callback final : private abstract_cancellation_callback {
	cancellation_callback(cancellation_token token, F functor)
	: _event{token._event}, _functor{std::move(functor)} {
		if(!_event)
			return;
		frg::unique_lock guard{_event->_mutex};
		if(_event->_was_requested) {
			_functor();
		}else{
			_event->_cbs.push_back(this);
		}
	}

	cancellation_callback(const cancellation_callback &) = delete;
	cancellation_callback(cancellation_callback &&) = delete;

	~cancellation_callback() {
		if(!_event)
			return;
		frg::unique_lock guard{_event->_mutex};
		if(!_event->_was_requested) {
			auto it = _event->_cbs.iterator_to(this);
			_event->_cbs.erase(it);
		}
	}

	cancellation_callback &operator= (const cancellation_callback &) = delete;
	cancellation_callback &operator= (cancellation_callback &&) = delete;

	void unbind() {
		if(!_event)
			return;
		frg::unique_lock guard{_event->_mutex};
		if(!_event->_was_requested) {
			auto it = _event->_cbs.iterator_to(this);
			_event->_cbs.erase(it);
		}
	}

private:
	void call() override {
		_functor();
	}

	cancellation_event *_event;
	F _functor;
};

template<typename F>
struct cancellation_observer final : private abstract_cancellation_callback {
	cancellation_observer(F functor = F{})
	: _event{nullptr}, _functor{std::move(functor)} {
	}

	cancellation_observer(const cancellation_observer &) = delete;
	cancellation_observer(cancellation_observer &&) = delete;

	// TODO: we could do some sanity checking of the state in the destructor.
	~cancellation_observer() = default;

	cancellation_observer &operator= (const cancellation_observer &) = delete;
	cancellation_observer &operator= (cancellation_observer &&) = delete;

	bool try_set(cancellation_token token) {
		assert(!_event);
		if(!token._event)
			return true;
		_event = token._event;

		frg::unique_lock guard{_event->_mutex};
		if(!_event->_was_requested) {
			_event->_cbs.push_back(this);
			return true;
		}
		return false;
	}

	// Either set up this observer or call into the handler.
	// Note that calling try_reset() afterwards is always safe.
	// In particular, try_set() only fails if cancellation has been triggered
	// and try_reset() will simply fail in that case.
	void force_set(cancellation_token token) {
		if(!try_set(token))
			_functor();
	}

	bool try_reset() {
		if(!_event)
			return true;

		frg::unique_lock guard{_event->_mutex};
		if(!_event->_was_requested) {
			auto it = _event->_cbs.iterator_to(this);
			_event->_cbs.erase(it);
			return true;
		}
		return false;
	}

	// TODO: provide a reset() method that waits until the cancellation handler completed.
	//       This can be done by spinning on an atomic variable in cancellation_event
	//       that determines whether handlers are currently running.

private:
	void call() override {
		_functor();
	}

	cancellation_event *_event;
	F _functor;
};

// Helper class to implement cancellation for low-level data structures.
// Both TryCancel and Resume are function objects that take a cancellation_resolver *.
// Cancellation always involves a potential race between the cancellation code path
// and the regular completion code path. This class helps to resolve this race.
//
// Usage:
// * Initialization: users do their internal book keeping (e.g., by adding nodes to their
//   data structures or similar) and then call listen() with a cancellation token.
// * Regular completion: on regular completion, users call complete().
//   This tries to unregister the cancellation_resolver from the cancellation event.
//   If this succeeds, tryCancel() will never be called.
//   If it fails, tryCancel() either was already called or it is called concurrently.
// * On cancellation, the tryCancel() callback is called.
//   If tryCancel() returns true, complete() will never be called.
//   If it fails, complete() either was already called or it is called concurrently.
// * resume() is called once all of the following hold:
//   - listen() is done.
//   - Either complete() is done or it will never be called.
//   - Either tryCancel() is done or it will never be called.
template<typename TryCancel, typename Resume>
struct cancellation_resolver final : private abstract_cancellation_callback {
	cancellation_resolver(TryCancel tryCancel = TryCancel{}, Resume resume = Resume{})
	: tryCancel_{std::move(tryCancel)}, resume_{std::move(resume)} { }

	cancellation_resolver(const cancellation_resolver &) = delete;

	// TODO: we could do some sanity checking of the state in the destructor.
	~cancellation_resolver() = default;

	cancellation_resolver &operator= (const cancellation_resolver &) = delete;

	void listen(cancellation_token ct) {
		event_ = ct._event;
		if (!event_) {
			transition_(done_listen | done_cancellation_path);
			return;
		}

		// Try to register a callback for cancellation.
		// This will succeed unless the cancellation event is already triggered.
		bool registered = false;
		{
			frg::unique_lock guard{event_->_mutex};
			if(!event_->_was_requested) {
				event_->_cbs.push_back(this);
				registered = true;
			}
		}
		if (registered) {
			transition_(done_listen);
			return;
		}

		// If we get here, the cancellation event was already triggered.
		// Do the equivalent of call(), except that we also set the done_listen bit.
		if (tryCancel_(this)) {
			transition_(done_listen | done_completion_path | done_cancellation_path);
			return;
		}
		transition_(done_listen | done_cancellation_path);
	}

	void complete() {
		transition_(done_completion_path);
	}

private:
	void call() override {
		if (tryCancel_(this)) {
			transition_(done_completion_path | done_cancellation_path);
			return;
		}
		transition_(done_cancellation_path);
	}

	void transition_(unsigned int new_bits) {
		assert(!(new_bits & ~(done_listen | done_completion_path | done_cancellation_path)));

		auto old_st = state_.fetch_or(new_bits, std::memory_order_acq_rel);
		assert(!(old_st & new_bits));

		// Both resume() and cancellation event unregistration only happen once both
		// listen() and the completion code path are done.
		auto st = old_st | new_bits;
		if (!(st & done_listen) || !(st & done_completion_path))
			return;

		if (!(st & done_cancellation_path)) {
			// Try to unregister from the cancellation event once both listen() and complete() were called.
			// Note that we enter this code path at most once since done_cancellation_path is the only missing
			// bit in state_ and the next call to transition_() will necessarily set it.
			assert(event_);

			frg::unique_lock guard{event_->_mutex};
			if (event_->_was_requested)
				return;
			auto it = event_->_cbs.iterator_to(this);
			event_->_cbs.erase(it);
		}

		// Call resume() when all code paths are done. This can only happen once.
		resume_(this);
	}

	// Set in state_ when listen() is done.
	static constexpr unsigned int done_listen = 1u << 0;
	// Set in state_ when complete() is done or when we know that it will never be called.
	static constexpr unsigned int done_completion_path = 1u << 1;
	// Set in state_ when tryCancel() is done or when we know that it will never be called.
	static constexpr unsigned int done_cancellation_path = 1u << 2;

	cancellation_event *event_{nullptr};
	std::atomic<unsigned int> state_{0};
	[[no_unique_address]] TryCancel tryCancel_;
	[[no_unique_address]] Resume resume_;
};

inline void cancellation_event::cancel() {
	frg::intrusive_list<
		abstract_cancellation_callback,
		frg::locate_member<
			abstract_cancellation_callback,
			frg::default_list_hook<abstract_cancellation_callback>,
			&abstract_cancellation_callback::_hook
		>
	> pending;

	{
		frg::unique_lock guard{_mutex};
		_was_requested = true;
		pending.splice(pending.begin(), _cbs);
	}

	while (!pending.empty()) {
		auto cb = pending.front();
		pending.pop_front();
		cb->call();
	}
}

inline void cancellation_event::reset() {
	frg::unique_lock guard{_mutex};
	_was_requested = false;
}

} // namespace async::detail

namespace async {

using detail::cancellation_event;
using detail::cancellation_token;
using detail::cancellation_callback;
using detail::cancellation_observer;
using detail::cancellation_resolver;

namespace {

template<std::size_t D, typename T, typename U>
auto make_uniform_array(U const& u) {
	return std::apply(
		[&](auto... e) {return std::array{((void) e, T(u))...};},
		std::array<int, D>{}
	);
}

}

template<typename Receiver, size_t N>
struct suspend_indefinitely_operation {
private:
	struct functor {
		functor(suspend_indefinitely_operation<Receiver, N> *op)
		: op_{op} { }

		void operator() () {
			op_->transition_(state_cancel);
		}

	private:
		suspend_indefinitely_operation<Receiver, N> *op_;
	};

public:
	suspend_indefinitely_operation(std::array<cancellation_token, N> cancellation, Receiver r)
	: cancellation_{cancellation}, r_{r}, obs_{make_uniform_array<N, cancellation_observer<functor>>(this)} { }

	void start() {
		for (size_t i = 0; i < N; ++i)
			obs_[i].force_set(cancellation_[i]);

		transition_(state_init);
	}

private:
	void transition_(size_t state) {
		assert(state == state_init || state_cancel);

		// Set the init or cancel bit.
		auto st = st_.fetch_or(state, std::memory_order_acq_rel);
		auto new_st = st | state;

		// We always decrement (st_ & state_ctr) by at least one.
		// Note that this ensures that set_value() is only called after all calls to
		// transition_() reach the fetch_sub() below.
		size_t ctr = 1;

		// Reset all observers once both init and cancel bits are set.
		auto init_and_cancel = [] (size_t s) {
			return (s & state_init) && (s & state_cancel);
		};
		if (!init_and_cancel(st) && init_and_cancel(new_st)) {
			for (size_t i = 0; i < N; ++i) {
				if(obs_[i].try_reset())
					++ctr;
			}
		}

		// Decrement (st_ & state_ctr).
		// Note that we must not touch the this object afterwards.
		auto final_st = st_.fetch_sub(ctr, std::memory_order_acq_rel);
		assert((final_st & state_ctr) >= ctr);
		if ((final_st & state_ctr) == ctr) {
			assert(final_st & state_init);
			assert(final_st & state_ctr);
			execution::set_value(r_);
		}
	}

	// st_ stores an atomic state variable. There are two kinds of state transition:
	// init and cancel. Both types of transition decrement (st_ & state_ctr).
	// Additionally, the transitions set the state_init and state_cancel bits, respectively.
	// When both state_init and state_cancel are set, we reset all cancellation observers.
	// Note that this happens only once.

	static constexpr size_t state_init = size_t(1) << 31;
	static constexpr size_t state_cancel = size_t(1) << 30;
	static constexpr size_t state_ctr = (size_t(1) << 30) - 1;

	std::array<cancellation_token, N> cancellation_;
	std::atomic<size_t> st_{N + 1};
	Receiver r_;
	std::array<cancellation_observer<functor>, N> obs_;
	bool started_ = false;
};

template<size_t N>
requires(N > 0)
struct suspend_indefinitely_sender {
	using value_type = void;

	std::array<cancellation_token, N> cancellation;
};

template<typename Receiver, size_t N>
suspend_indefinitely_operation<Receiver, N> connect(suspend_indefinitely_sender<N> s, Receiver r) {
	return {s.cancellation, std::move(r)};
}

template<size_t N>
inline sender_awaiter<suspend_indefinitely_sender<N>> operator co_await(suspend_indefinitely_sender<N> s) {
	return {s};
}

template<typename... C>
requires(std::convertible_to<C, cancellation_token>&&...)
inline suspend_indefinitely_sender<sizeof...(C)> suspend_indefinitely(C&&... cts) {
	return {std::array<cancellation_token, sizeof...(C)>{cts...}};
}

//---------------------------------------------------------------------------------------
// with_cancel_cb()
//---------------------------------------------------------------------------------------

template <typename R, Sender S, typename Cb>
requires Receives<R, typename S::value_type>
struct with_cancel_cb_operation {
	using value_type = typename S::value_type;

	with_cancel_cb_operation(S s, Cb cb, cancellation_token ct, R dr)
	:  op_{execution::connect(std::move(s), intermediate_receiver{this})},
		cb_{std::move(cb)},
		dr_{std::move(dr)},
		ct_{ct} { }

	with_cancel_cb_operation(const with_cancel_cb_operation &) = delete;

	with_cancel_cb_operation &operator=(const with_cancel_cb_operation &) = delete;

	void start() {
		cobs_.force_set(ct_);
		execution::start(op_);
	}

private:
	struct intermediate_receiver {
		template <typename ...Args>
		void set_value(Args &&...args) {
			self->value_.emplace(std::forward<Args>(args)...);

			// If try_reset() succeeds, the operation was not cancelled and cancel_state_ is irrelevant.
			if (self->cobs_.try_reset()
					|| self->cancel_state_.fetch_sub(1, std::memory_order_acq_rel) == 1)
				self->complete_();
		}

		auto get_env() {
			return execution::get_env(self->dr_);
		}

		with_cancel_cb_operation *self;
	};
	static_assert(Receives<intermediate_receiver, value_type>);

	struct cancel_handler {
		void operator()() {
			self->cb_();

			if (self->cancel_state_.fetch_sub(1, std::memory_order_acq_rel) == 1)
				self->complete_();
		}

		with_cancel_cb_operation *self;
	};

	void complete_() {
		assert(value_);
		if constexpr (std::is_same_v<value_type, void>) {
			execution::set_value(std::move(dr_));
		} else {
			execution::set_value(std::move(dr_), std::move(*value_));
		}
	}

	execution::operation_t<S, intermediate_receiver> op_;
	Cb cb_;
	R dr_;
	cancellation_token ct_;
	cancellation_observer<cancel_handler> cobs_{cancel_handler{this}};

	// Valid if cancellation is triggered before try_reset():
	// 2: Both cb_() and the operation are still running.
	// 1: Either cb_() or the operation (both not both) are still running.
	// 0: Both cb_() and the operation are done.
	std::atomic<int> cancel_state_{2};

	struct empty { };

	std::optional<
		std::conditional_t<
			std::is_same_v<value_type, void>,
			empty,
			value_type
		>
	> value_;
};

template <Sender S, typename Cb>
struct [[nodiscard]] with_cancel_cb_sender {
	using value_type = typename S::value_type;

	template<typename R>
	requires Receives<R, value_type>
	friend with_cancel_cb_operation<R, S, Cb>
	connect(with_cancel_cb_sender s, R r) {
		return {std::move(s.s), std::move(s.cb), s.ct, std::move(r)};
	}

	S s;
	Cb cb;
	cancellation_token ct;
};

template <Sender S, typename Cb>
with_cancel_cb_sender<S, Cb> with_cancel_cb(S s, Cb cb, cancellation_token ct) {
	return {std::move(s), std::move(cb), ct};
}

template <Sender S, typename Cb>
sender_awaiter<with_cancel_cb_sender<S, Cb>, typename S::value_type>
operator co_await(with_cancel_cb_sender<S, Cb> s) {
	return {std::move(s)};
}

} // namespace async
