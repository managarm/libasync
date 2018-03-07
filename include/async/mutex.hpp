#ifndef ASYNC_MUTEX_HPP
#define ASYNC_MUTEX_HPP

#include <deque>
#include <async/result.hpp>

namespace async {

namespace detail {
	struct mutex {
		struct node : smarter::counter, awaitable<void> {
			node()
			: _flags(0) {
				setup(smarter::adopt_rc, nullptr, 2);
			}

			node(const node &) = delete;

			node &operator= (const node &) = delete;

			void dispose() override {
				auto f = _flags.fetch_or(has_awaiter, std::memory_order_acq_rel);
				assert(f & has_value);
				delete this;
			}

			void serve() {
				auto f = _flags.fetch_or(has_value, std::memory_order_acq_rel);
				assert(!(f & has_value));
				if(f & has_awaiter) {
					if(_awaiter)
						_awaiter();
				}
			}

			void then(callback<void()> awaiter) override {
				_awaiter = awaiter;

				auto f = _flags.fetch_or(has_awaiter, std::memory_order_acq_rel);
				assert(!(f & has_awaiter));
				if(f & has_value) {
					_awaiter();
				}
			}

		private:
			std::atomic<int> _flags;
			callback<void()> _awaiter;
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
					item->serve();
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
				item->serve();
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
