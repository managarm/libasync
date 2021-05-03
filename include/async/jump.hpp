#ifndef ASYNC_JUMP_HPP
#define ASYNC_JUMP_HPP

#include <deque>

#include <async/result.hpp>
#include <frg/list.hpp>

namespace async {

struct jump {
	struct awaiter {
		friend struct jump;

	private:
		explicit awaiter(jump *owner)
		: _owner{owner} { }

	public:
		bool await_ready() {
			return false;
		}

		template<typename H>
		void await_suspend(H handle) {
			_cb = callback<void()>{[address = handle.address()] {
				auto handle = H::from_address(address);
				handle.resume();
			}};

			bool done;
			{
				frg::unique_lock lock(_owner->_mutex);
				done = _owner->_done;
				if(!done)
					_owner->_waiters.push_back(this);
			}

			if(done)
				handle.resume();
		}

		void await_resume() {
			// Just return void.
		}

	private:
		jump *_owner;
		frg::default_list_hook<awaiter> _hook;
		callback<void()> _cb;
	};

	jump()
	: _done(false) { }

	void trigger() {
		frg::intrusive_list<
			awaiter,
			frg::locate_member<
				awaiter,
				frg::default_list_hook<awaiter>,
				&awaiter::_hook
			>
		> items;
		{
			frg::unique_lock lock(_mutex);
			_done = true;
			items.splice(items.end(), _waiters);
		}

		// Invoke the callbacks without holding locks.
		while(!items.empty()) {
			auto item = items.front();
			items.pop_front();
			item->_cb();
		}
	}

	// TODO: This emulation is a bad idea from a performance point-of-view. Refactor it.
	awaiter async_wait() {
		return awaiter{this};
	}

	void reset() {
		// TODO: We do not need the lock for exclusion.
		// Do we need it for the memory barrier?
		frg::unique_lock lock(_mutex);
		_done = false;
	}

private:
	platform::mutex _mutex;
	bool _done;
	frg::intrusive_list<
		awaiter,
		frg::locate_member<
			awaiter,
			frg::default_list_hook<awaiter>,
			&awaiter::_hook
		>
	> _waiters;
};

} // namespace async

#endif // ASYNC_JUMP_HPP
