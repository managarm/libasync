#ifndef ASYNC_DOORBELL_HPP
#define ASYNC_DOORBELL_HPP

#include <async/result.hpp>
#include <async/cancellation.hpp>
#include <boost/intrusive/list.hpp>

namespace async {

namespace detail {
	struct doorbell {
		enum class state {
			null,
			queued,
			cancelled,
			done
		};

		struct node : awaitable<void>,
				boost::intrusive::list_base_hook<> {
			friend struct doorbell;

			using awaitable<void>::set_ready;

			node(doorbell *owner, cancellation_token cancellation)
			: _owner{owner}, _state{state::null},
					_cancel_cb{cancellation, cancel_handler{this}} { }

			node(const node &) = delete;

			node &operator= (const node &) = delete;

			void submit() override {
				std::lock_guard<std::mutex> lock(_owner->_mutex);
				if(_state == state::cancelled) {
					set_ready();
					return;
				}
				assert(_state == state::null);
				_state = state::queued;
				_owner->_queue.push_back(*this);
			}

			void dispose() override {
				assert(ready());
				delete this;
			}

		private:
			struct cancel_handler {
				void operator() () {
					self->_owner->cancel_async_wait(self);
				}

				node *self;
			};

			doorbell *_owner;

			// This field is protected by the _mutex.
			state _state;

			cancellation_callback<cancel_handler> _cancel_cb;
		};

		void ring() {
			// Grab all items and mark them as retired while we hold the lock.
			boost::intrusive::list<node> items;
			{
				std::lock_guard<std::mutex> lock(_mutex);

				items.splice(items.end(), _queue);
				for(auto &ref : items) {
					assert(ref._state == state::queued);
					ref._state = state::done;
				}
			}

			// Now invoke the individual callbacks.
			while(!items.empty()) {
				auto item = &items.front();
				items.pop_front();
				item->set_ready();
			}
		}

		result<void> async_wait(cancellation_token cancellation = {}) {
			return result<void>{new node{this, cancellation}};
		}

		void cancel_async_wait(result_reference<void> future) {
			auto item = static_cast<node *>(future.get_awaitable());
			return cancel_async_wait(item);
		}

		void cancel_async_wait(node *item) {
			bool became_ready = false;
			{
				std::lock_guard<std::mutex> lock(_mutex);
				
				if(item->_state == state::done)
					return;

				if(item->_state == state::queued) {
					auto it = boost::intrusive::list<node>::s_iterator_to(*item);
					_queue.erase(it);
					became_ready = true;
				}else
					assert(item->_state == state::null);
				item->_state = state::cancelled;
			}

			if(became_ready)
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
