#ifndef ASYNC_DOORBELL_HPP
#define ASYNC_DOORBELL_HPP

#include <async/result.hpp>
#include <boost/intrusive/list.hpp>

namespace async {

namespace detail {
	struct doorbell {
		struct node : awaitable<void>,
				boost::intrusive::list_base_hook<> {
			using awaitable<void>::set_ready;

			node(doorbell *owner)
			: _retired{false} { }

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

			bool is_retired() {
				return _retired;
			}

		private:
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
			}
		}

		result<void> async_wait() {
			auto item = new node{this};
			{
				std::lock_guard<std::mutex> lock(_mutex);
				_queue.push_back(*item);
			}

			return result<void>{item};
		}

		void cancel_async_wait(result_reference<void> future) {
			auto item = static_cast<node *>(future.get_awaitable());
			{
				std::lock_guard<std::mutex> lock(_mutex);
				
				if(item->is_retired())
					return;

				auto it = boost::intrusive::list<node>::s_iterator_to(*item);
				_queue.erase(it);
				item->retire();
			}

			item->set_ready();
		}

	private:
		std::mutex _mutex;
		boost::intrusive::list<node> _queue;
	};
}

using detail::doorbell;

} // namespace async

#endif // ASYNC_DOORBELL_HPP
