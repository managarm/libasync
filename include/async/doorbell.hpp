#ifndef ASYNC_DOORBELL_HPP
#define ASYNC_DOORBELL_HPP

#include <async/result.hpp>
#include <boost/intrusive/list.hpp>

namespace async {

namespace detail {
	// Lock order: doorbell::_queue_mutex < node::_mutex
	struct doorbell {
		struct node : cancelable_awaitable<void>, boost::intrusive::list_base_hook<> {
			node(doorbell *owner)
			: _owner{owner}, _fired{false} { }

			node(const node &) = delete;

			node &operator= (const node &) = delete;

			void pretrigger() {
				std::lock_guard<std::mutex> lock(_mutex);
				_fired = true;
			}

			void trigger() {
				callback<void()> cb;
				{
					std::lock_guard<std::mutex> lock(_mutex);

					cb = std::exchange(_cb, callback<void()>{});
				}

				if(cb)
					cb();
			}

			void then(callback<void()> cb) override {
				bool already_fired;
				{
					std::lock_guard<std::mutex> lock(_mutex);
					assert(!_cb);

					already_fired = _fired;
					if(!already_fired)
						_cb = cb;
				}

				if(already_fired)
					cb();
			}

			void cancel() override {
				callback<void()> cb;
				{
					std::lock_guard<std::mutex> queue_lock(_owner->_queue_mutex);
					std::lock_guard<std::mutex> lock(_mutex);
					
					if(_fired)
						return;

					auto it = boost::intrusive::list<node>::s_iterator_to(*this);
					_owner->_queue.erase(it);
					cb = std::exchange(_cb, callback<void()>{});
					_fired = true;
				}

				if(cb)
					cb();
			}

			void detach() override {
				assert(_fired); // This assumption is false if we use cancel().
				// We inspect _cb without holding the lock.
				// We someone changes it concurrently, the contract is broken anyway.
				assert(!_cb);
				delete this;
			}

		private:
			doorbell *_owner;

			std::mutex _mutex;
			bool _fired;
			callback<void()> _cb;
		};

		void ring() {
			boost::intrusive::list<node> items;
			{
				std::lock_guard<std::mutex> queue_lock(_queue_mutex);

				// Grab all items and mark them as retired while we hold the lock.
				items.splice(items.end(), _queue);
				for(auto &ref : items)
					ref.pretrigger();
			}

			for(auto &ref : items)
				ref.trigger();
		}

		cancelable_result<void> async_wait() {
			auto item = new node{this};
			{
				std::lock_guard<std::mutex> queue_lock(_queue_mutex);
				_queue.push_back(*item);
			}
			return cancelable_result<void>{item};
		}

	private:
		std::mutex _queue_mutex;
		boost::intrusive::list<node> _queue;
	};
}

using detail::doorbell;

} // namespace async

#endif // ASYNC_DOORBELL_HPP
