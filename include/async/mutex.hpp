#ifndef ASYNC_MUTEX_HPP
#define ASYNC_MUTEX_HPP

#include <frg/list.hpp>
#include <async/result.hpp>

namespace async {

namespace detail {
	struct mutex {
		struct node : awaitable<void> {
			friend mutex;
			using awaitable<void>::set_ready;

			node(mutex *owner)
			: _owner{owner} { }

			node(const node &) = delete;

			node &operator= (const node &) = delete;

			void submit() override {
				frg::unique_lock lock(_owner->_mutex);

				if(_owner->_locked) {
					_owner->_waiters.push_back(this);
				}else{
					set_ready();
					_owner->_locked = true;
				}
			}

			void dispose() override {
				assert(ready());
				delete this;
			}

		private:
			mutex *_owner;
			frg::default_list_hook<node> _hook;
		};

		mutex()
		: _locked{false} { }

		result<void> async_lock() {
			return result<void>{new node{this}};
		}

		bool try_lock() {
			frg::unique_lock lock(_mutex);

			if (_locked)
				return false;

			_locked = true;
			return true;
		}

		void unlock() {
			frg::unique_lock lock(_mutex);
			assert(_locked);

			if(_waiters.empty()) {
				_locked = false;
			}else{
				auto item = _waiters.pop_front();
				item->set_ready();
			}
		}

	private:
		platform::mutex _mutex;
		bool _locked;
		frg::intrusive_list<
			node,
			frg::locate_member<
				node,
				frg::default_list_hook<node>,
				&node::_hook
			>
		> _waiters;
	};
}

using detail::mutex;

} // namespace async

#endif // ASYNC_MUTEX_HPP
