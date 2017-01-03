#ifndef ASYNC_DOORBELL_HPP
#define ASYNC_DOORBELL_HPP

#include <vector>
#include <async/result.hpp>

namespace async {

namespace detail {
	struct doorbell {
		struct node : awaitable<void> {
			node()
			: _flags(0) { }

			node(const node &) = delete;

			node &operator= (const node &) = delete;

			void trigger() {
				auto f = _flags.fetch_or(has_value, std::memory_order_acq_rel);
				assert(!(f & has_value));
				if(f & has_awaiter) {
					if(_awaiter)
						_awaiter();
					delete this;
				}
			}

			void then(callback<void()> awaiter) override {
				_awaiter = awaiter;

				auto f = _flags.fetch_or(has_awaiter, std::memory_order_acq_rel);
				assert(!(f & has_awaiter));
				if(f & has_value) {
					_awaiter();
					delete this;
				}
			}

			void detach() override {
				auto f = _flags.fetch_or(has_awaiter, std::memory_order_acq_rel);
				assert(!(f & has_awaiter));
				if(f & has_value)
					delete this;
			}

		private:
			std::atomic<int> _flags;
			callback<void()> _awaiter;
		};

		void ring() {
			std::vector<node *> items;
			{
				std::lock_guard<std::mutex> lock(_mutex);
				items = std::move(_items);
				_items.clear();
			}

			for(auto item : items)
				item->trigger();
		}

		result<void> async_wait() {
			auto item = new node;
			{
				std::lock_guard<std::mutex> lock(_mutex);
				_items.push_back(item);
			}
			return result<void>{item};
		}

	private:
		std::mutex _mutex;
		// TODO: Make this list intrusive.
		std::vector<node *> _items;
	};
}

using detail::doorbell;

} // namespace async

#endif // ASYNC_DOORBELL_HPP
