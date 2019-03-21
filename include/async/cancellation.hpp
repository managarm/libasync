#ifndef LIBASYNC_CANCELLATION_HPP
#define LIBASYNC_CANCELLATION_HPP

#include <boost/intrusive/list.hpp>
#include <mutex>

namespace async::detail {

struct abstract_cancellation_callback {
	friend struct cancellation_event;

private:
	virtual void call() = 0;

	boost::intrusive::list_member_hook<> _hook;
};

struct cancellation_event {
	friend struct cancellation_token;

	template<typename F>
	friend struct cancellation_callback;

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
	std::mutex _mutex;

	bool _was_requested;

	boost::intrusive::list<
		abstract_cancellation_callback,
		boost::intrusive::member_hook<
			abstract_cancellation_callback,
			boost::intrusive::list_member_hook<>,
			&abstract_cancellation_callback::_hook
		>
	> _cbs;
};

struct cancellation_token {
	template<typename F>
	friend struct cancellation_callback;

	cancellation_token()
	: _event{nullptr} { }

	cancellation_token(cancellation_event &event_ref)
	: _event{&event_ref} { }

	bool is_cancellation_requested() const {
		if(!_event)
			return false;
		std::lock_guard guard{_event->_mutex};
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
		std::lock_guard guard{_event->_mutex};
		if(_event->_was_requested) {
			_functor();
		}else{
			_event->_cbs.push_back(*this);
		}
	}

	cancellation_callback(const cancellation_callback &) = delete;
	cancellation_callback(cancellation_callback &&) = delete;

	~cancellation_callback() {
		if(!_event)
			return;
		std::lock_guard guard{_event->_mutex};
		if(!_event->_was_requested) {
			auto it = _event->_cbs.iterator_to(*this);
			_event->_cbs.erase(it);
		}
	}

	cancellation_callback &operator= (const cancellation_callback &) = delete;
	cancellation_callback &operator= (cancellation_callback &&) = delete;

private:
	void call() override {
		_functor();
	}

	cancellation_event *_event;
	F _functor;
};

inline void cancellation_event::cancel() {
	std::lock_guard guard{_mutex};
	_was_requested = true;
	for(abstract_cancellation_callback &cb : _cbs)
		cb.call();
	_cbs.clear();
}

inline void cancellation_event::reset() {
	std::lock_guard guard{_mutex};
	_was_requested = false;
}

} // namespace async::detail

namespace async {

using detail::cancellation_event;
using detail::cancellation_token;
using detail::cancellation_callback;

} // namespace async

#endif // LIBASYNC_CANCELLATION_HPP
