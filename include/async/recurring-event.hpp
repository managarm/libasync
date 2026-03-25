#pragma once

#include <async/algorithm.hpp>
#include <async/basic.hpp>
#include <async/cancellation.hpp>
#include <frg/expected.hpp>
#include <frg/container_of.hpp>
#include <frg/list.hpp>

namespace async {

struct recurring_event {
private:
	enum class state {
		none,
		submitted,
		pending,
		cancelled
	};

	struct node {
		friend struct recurring_event;

		node()
		: st_{state::none} { }

		node(const node &) = delete;

		node &operator= (const node &) = delete;

		virtual void complete() = 0;

		bool was_cancelled() const { return st_ == state::cancelled; }

	protected:
		virtual ~node() = default;

	private:
		// Protected by _mutex.
		frg::default_list_hook<node> _hook;
		// The submitted -> pending transition is protected by _mutex.
		state st_;
	};

public:
	void raise() {
		// Grab all items and mark them as pending while we hold the lock.
		frg::intrusive_list<
			node,
			frg::locate_member<
				node,
				frg::default_list_hook<node>,
				&node::_hook
			>
		> items;
		{
			frg::unique_lock lock(_mutex);

			items.splice(items.end(), queue_);
			for(auto item : items) {
				assert(item->st_ == state::submitted);
				item->st_ = state::pending;
			}
		}

		// Now invoke the individual callbacks.
		while(!items.empty()) {
			auto item = items.front();
			items.pop_front();
			item->complete();
		}
	}

	bool try_cancel(node *nd) {
		frg::unique_lock lock(_mutex);

		if(nd->st_ == state::submitted) {
			nd->st_ = state::cancelled;
			auto it = queue_.iterator_to(nd);
			queue_.erase(it);
			return true;
		}
		return false;
	}

	// ----------------------------------------------------------------------------------
	// async_wait_if() and its boilerplate.
	// ----------------------------------------------------------------------------------

	template<typename C, typename Receiver>
	struct wait_if_operation final : private node {
		wait_if_operation(recurring_event *evt, C cond, cancellation_token ct, Receiver r)
		: evt_{evt}, cond_{std::move(cond)}, ct_{std::move(ct)}, r_{std::move(r)} { }

		void start() {
			assert(st_ == state::none);

			bool fast_path = false;
			{
				frg::unique_lock lock(evt_->_mutex);

				if(!cond_()) {
					st_ = state::pending;
					fast_path = true;
				}else{
					st_ = state::submitted;
					evt_->queue_.push_back(this);
				}
			}

			if(fast_path) {
				return execution::set_value(r_, maybe_awaited::condition_failed);
			}
			cr_.listen(ct_);
		}

	private:
		struct try_cancel_fn {
			bool operator()(auto *cr) {
				auto self = frg::container_of(cr, &wait_if_operation::cr_);
				return self->evt_->try_cancel(self);
			}
		};
		struct resume_fn {
			void operator()(auto *cr) {
				auto self = frg::container_of(cr, &wait_if_operation::cr_);
				if(self->was_cancelled())
					execution::set_value(self->r_, maybe_cancelled::cancelled);
				else
					execution::set_value(self->r_, maybe_awaited::awaited);
			}
		};

		void complete() override {
			cr_.complete();
		}

		recurring_event *evt_;
		C cond_;
		cancellation_token ct_;
		Receiver r_;
		cancellation_resolver<try_cancel_fn, resume_fn> cr_;
	};

	template<typename C>
	struct wait_if_sender {
		using value_type = frg::expected<maybe_cancelled, maybe_awaited>;

		template<typename Receiver>
		friend wait_if_operation<C, Receiver> connect(wait_if_sender s, Receiver r) {
			return {s.evt, std::move(s.cond), s.ct, std::move(r)};
		}

		friend sender_awaiter<wait_if_sender, wait_if_sender<C>::value_type> operator co_await (wait_if_sender s) {
			return {s};
		}

		recurring_event *evt;
		C cond;
		cancellation_token ct;
	};

	template<typename C>
	auto async_wait_if(C cond) {
		return transform(wait_if_sender<C>{this, std::move(cond), async::cancellation_token{}},
			[](wait_if_sender<C>::value_type result) -> bool {
				assert(result);
				return result.value() == maybe_awaited::awaited;
			});
	}

	template<typename C>
	wait_if_sender<C> async_wait_if(C cond, cancellation_token ct) {
		return {this, std::move(cond), ct};
	}

	// Wait without checking for a condition. This is only really useful in single-threaded
	// code, or when wakeup may be missed without causing confusion.
	// returns true on successful await, and false on cancellation
	auto async_wait(cancellation_token ct = {}) {
		auto c = [] () -> bool { return true; };
		return async::transform(wait_if_sender<decltype(c)>{this, c, ct}, [](wait_if_sender<decltype(c)>::value_type s) -> bool {
			// the condition above can never fail
			assert(!s || s.value() != maybe_awaited::condition_failed);

			return bool(s);
		});
	}

private:
	platform::mutex _mutex;

	frg::intrusive_list<
		node,
		frg::locate_member<
			node,
			frg::default_list_hook<node>,
			&node::_hook
		>
	> queue_;
};

} // namespace async
