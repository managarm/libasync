#pragma once

#include <array>
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

} // namespace async
