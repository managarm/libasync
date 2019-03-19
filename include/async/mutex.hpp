#ifndef ASYNC_MUTEX_HPP
#define ASYNC_MUTEX_HPP

#include <deque>
#include <async/result.hpp>

namespace async {

namespace detail {
	struct mutex {
		struct node : awaitable<void> {
			using awaitable<void>::set_ready;

			node(mutex *owner)
			: _owner{owner} { }

			node(const node &) = delete;

			node &operator= (const node &) = delete;

			void submit() override {
				std::lock_guard<std::mutex> lock(_owner->_mutex);

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
		};

		mutex()
		: _locked{false} { }

		result<void> async_lock() {
			return result<void>{new node{this}};
		}

		void unlock() {
			std::lock_guard<std::mutex> lock(_mutex);
			assert(_locked);

			if(_waiters.empty()) {
				_locked = false;
			}else{
				auto item = _waiters.front();
				_waiters.pop_front();
				item->set_ready();
			}
		}

	private:
		std::mutex _mutex;
		bool _locked;
		// TODO: Make this list intrusive.
		std::deque<node *> _waiters;
	};
}

using detail::mutex;

} // namespace async

#endif // ASYNC_MUTEX_HPP
