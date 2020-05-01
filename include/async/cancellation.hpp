#pragma once

#include <frg/list.hpp>
#include "basic.hpp"

namespace async::detail {

struct abstract_cancellation_callback {
	friend struct cancellation_event;

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
struct cancellation_callback : abstract_cancellation_callback {
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
struct cancellation_observer : abstract_cancellation_callback {
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

	// TODO: provide a set() method that calls the handler inline if cancellation is requested.

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
	frg::unique_lock guard{_mutex};
	_was_requested = true;
	for(abstract_cancellation_callback *cb : _cbs)
		cb->call();
	_cbs.clear();
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

template<typename Receiver>
struct suspend_indefinitely_operation {
private:
	struct functor {
		functor(suspend_indefinitely_operation *op)
		: op_{op} { }

		void operator() () {
			op_->r_.set_value();
		}

	private:
		suspend_indefinitely_operation *op_;
	};

public:
	suspend_indefinitely_operation(cancellation_token cancellation, Receiver r)
	: cancellation_{cancellation}, r_{r}, obs_{this} { }

	void start() {
		if(!obs_.try_set(cancellation_))
			r_.set_value();
	}

private:
	cancellation_token cancellation_;
	Receiver r_;
	cancellation_observer<functor> obs_;
	bool started_ = false;
};

struct suspend_indefinitely_sender {
	cancellation_token cancellation;
};

template<typename Receiver>
suspend_indefinitely_operation<Receiver> connect(suspend_indefinitely_sender s, Receiver r) {
	return {s.cancellation, std::move(r)};
}

inline sender_awaiter<suspend_indefinitely_sender> operator co_await(suspend_indefinitely_sender s) {
	return {s};
}

inline suspend_indefinitely_sender suspend_indefinitely(cancellation_token cancellation) {
	return {cancellation};
}

} // namespace async
