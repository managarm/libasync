#pragma once

#include <async/cancellation.hpp>
#include <frg/functional.hpp>
#include <frg/list.hpp>

namespace async {

struct recurring_event {
private:
	enum class state {
		none,
		submitted,
		pending,
		retired
	};

	struct node {
		friend struct recurring_event;

		node()
		: st_{state::none} { }

		node(const node &) = delete;

		node &operator= (const node &) = delete;

		virtual void complete() = 0;

	private:
		// Protected by _mutex.
		frg::default_list_hook<node> _hook;
		// The submitted -> pending transition is protected by _mutex.
		state st_;
	};

public:
	void raise() {
		// Grab all items and mark them as retired while we hold the lock.
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

	// ----------------------------------------------------------------------------------
	// async_wait_if() and its boilerplate.
	// ----------------------------------------------------------------------------------

	template<typename C, typename Receiver>
	struct wait_if_operation final : private node {
		wait_if_operation(recurring_event *evt, C cond, cancellation_token ct, Receiver r)
		: evt_{evt}, cond_{std::move(cond)}, ct_{std::move(ct)}, r_{std::move(r)}, cobs_{this} { }

		void start() {
			assert(st_ == state::none);

			bool retire_condfail = false;
			bool retire_cancelled = false;
			{
				frg::unique_lock lock(evt_->_mutex);

				if(!cond_()) {
					st_ = state::pending;
					retire_condfail = true;
				}else if(!cobs_.try_set(ct_)) {
					st_ = state::pending;
					cancelled_ = true;
					retire_cancelled = true;
				}else{
					st_ = state::submitted;
					evt_->queue_.push_back(this);
				}
			}

			if(retire_condfail) {
				st_ = state::retired;
				r_.set_value(false);
			}else if(retire_cancelled) {
				st_ = state::retired;
				r_.set_value(true);
			}
		}

	private:
		void cancel() {
			{
				frg::unique_lock lock(evt_->_mutex);

				if(st_ == state::submitted) {
					st_ = state::pending;
					cancelled_ = true;
					auto it = evt_->queue_.iterator_to(this);
					evt_->queue_.erase(it);
				}else{
					assert(st_ == state::pending);
				}
			}

			st_ = state::retired;
			r_.set_value(true);
		}

		void complete() override {
			if(cobs_.try_reset()) {
				st_ = state::retired;
				r_.set_value(true);
			}
		}

		recurring_event *evt_;
		C cond_;
		cancellation_token ct_;
		Receiver r_;
		cancellation_observer<frg::bound_mem_fn<&wait_if_operation::cancel>> cobs_;
		bool cancelled_ = false;
	};

	template<typename C>
	struct wait_if_sender {
		using value_type = bool;

		template<typename Receiver>
		friend wait_if_operation<C, Receiver> connect(wait_if_sender s, Receiver r) {
			return {s.evt, std::move(s.cond), s.ct, std::move(r)};
		}

		friend sender_awaiter<wait_if_sender, bool> operator co_await (wait_if_sender s) {
			return {s};
		}

		recurring_event *evt;
		C cond;
		cancellation_token ct;
	};

	template<typename C>
	wait_if_sender<C> async_wait_if(C cond, cancellation_token ct = {}) {
		return {this, std::move(cond), ct};
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
