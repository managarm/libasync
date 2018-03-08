#ifndef ASYNC_DOORBELL_HPP
#define ASYNC_DOORBELL_HPP

#include <async/result.hpp>
#include <boost/intrusive/list.hpp>

namespace async {

namespace detail {
	struct doorbell {
		struct node : smarter::counter, cancelable_awaitable<void>,
				boost::intrusive::list_base_hook<> {
			using cancelable_awaitable<void>::set_ready;

			node(doorbell *owner)
			: _owner{owner}, _retired{false} {
				setup(smarter::adopt_rc, nullptr, 2);
			}

			node(const node &) = delete;

			node &operator= (const node &) = delete;

			void dispose() override {
				assert(ready());
				delete this;
			}

			void retire() {
				assert(!_retired);
				_retired = true;
			}

			void cancel() override {
				callback<void()> cb;
				{
					std::lock_guard<std::mutex> lock(_owner->_mutex);
					
					if(_retired)
						return;

					auto it = boost::intrusive::list<node>::s_iterator_to(*this);
					_owner->_queue.erase(it);
					_retired = true;
				}

				set_ready();
				decrement();
			}

		private:
			doorbell *_owner;

			// This field is protected by the _mutex.
			bool _retired;
		};

		void ring() {
			// Grab all items and mark them as retired while we hold the lock.
			boost::intrusive::list<node> items;
			{
				std::lock_guard<std::mutex> lock(_mutex);

				items.splice(items.end(), _queue);
				for(auto &ref : items)
					ref.retire();
			}

			// Now invoke the individual callbacks.
			while(!items.empty()) {
				auto item = &items.front();
				items.pop_front();

				item->set_ready();
				item->decrement();
			}
		}

		cancelable_result<void> async_wait() {
			auto item = new node{this};
			{
				std::lock_guard<std::mutex> lock(_mutex);
				_queue.push_back(*item);
			}

			smarter::shared_ptr<cancelable_awaitable<void>> ptr{smarter::adopt_rc, item, item};
			return cancelable_result<void>{std::move(ptr)};
		}

	private:
		std::mutex _mutex;
		boost::intrusive::list<node> _queue;
	};
}

using detail::doorbell;

} // namespace async

#endif // ASYNC_DOORBELL_HPP
