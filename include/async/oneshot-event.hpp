#pragma once

#include <async/cancellation.hpp>
#include <frg/functional.hpp>
#include <frg/list.hpp>

namespace async {

struct oneshot_event {
private:
	struct node {
		friend struct oneshot_event;

		node() = default;

		node(const node &) = delete;

		node &operator= (const node &) = delete;

		virtual void complete() = 0;

	protected:
		~node() = default;

	private:
		// Protected by mutex_.
		frg::default_list_hook<node> _hook;
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
			frg::unique_lock lock(mutex_);
			assert(!raised_);

			items.splice(items.end(), queue_);
			raised_ = true;
		}

		// Now invoke the individual callbacks.
		while(!items.empty()) {
			auto item = items.front();
			items.pop_front();
			item->complete();
		}
	}

	// ----------------------------------------------------------------------------------
	// wait() and its boilerplate.
	// ----------------------------------------------------------------------------------

	template<typename Receiver>
	struct wait_operation final : private node {
		wait_operation(oneshot_event *evt, cancellation_token ct, Receiver r)
		: evt_{evt}, ct_{std::move(ct)}, r_{std::move(r)}, cobs_{this} { }

		void start() {
			bool cancelled = false;
			{
				frg::unique_lock lock(evt_->mutex_);

				if(!evt_->raised_) {
					if(!cobs_.try_set(ct_)) {
						cancelled = true;
					}else{
						evt_->queue_.push_back(this);
						return;
					}
				}
			}

			execution::set_value(r_, !cancelled);
		}

	private:
		void cancel() {
			bool cancelled = false;
			{
				frg::unique_lock lock(evt_->mutex_);

				if(!evt_->raised_) {
					cancelled = true;
					auto it = evt_->queue_.iterator_to(this);
					evt_->queue_.erase(it);
				}
			}

			execution::set_value(r_, !cancelled_);
		}

		void complete() override {
			if(cobs_.try_reset())
				execution::set_value(r_, true);
		}

		oneshot_event *evt_;
		cancellation_token ct_;
		Receiver r_;
		cancellation_observer<frg::bound_mem_fn<&wait_operation::cancel>> cobs_;
		bool cancelled_ = false;
	};

	struct [[nodiscard]] wait_sender {
		using value_type = bool;

		template<typename Receiver>
		friend wait_operation<Receiver> connect(wait_sender s, Receiver r) {
			return {s.evt, s.ct, std::move(r)};
		}

		sender_awaiter<wait_sender, bool> operator co_await () {
			return {*this};
		}

		oneshot_event *evt;
		cancellation_token ct;
	};

	wait_sender wait(cancellation_token ct = {}) {
		return {this, ct};
	}

private:
	platform::mutex mutex_;

	bool raised_ = false;

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
