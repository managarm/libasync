#ifndef ASYNC_MUTEX_HPP
#define ASYNC_MUTEX_HPP

#include <deque>
#include <async/result.hpp>

namespace async {

namespace detail {
	struct mutex {
		struct node : smarter::counter, awaitable<void> {
			using awaitable<void>::set_ready;

			node() {
				setup(smarter::adopt_rc, nullptr, 2);
			}

			node(const node &) = delete;

			node &operator= (const node &) = delete;

			void dispose() override {
				assert(ready());
				delete this;
			}
		};

		mutex()
		: _locked{false} { }

		result<void> async_lock() {
			auto item = new node;
			{
				std::lock_guard<std::mutex> lock(_mutex);

				if(_locked) {
					_waiters.push_back(item);
				}else{
					item->set_ready();
					item->decrement();
					_locked = true;
				}
			}

			smarter::shared_ptr<awaitable<void>> ptr{smarter::adopt_rc, item, item};
			return result<void>{std::move(ptr)};
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
				item->decrement();
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
