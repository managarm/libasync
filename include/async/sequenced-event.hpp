#pragma once

#include <async/cancellation.hpp>
#include <frg/functional.hpp>
#include <frg/list.hpp>
#include <atomic>

namespace async {

struct sequenced_event {
private:
	struct node {
		friend struct sequenced_event;

		node() = default;

		node(const node &) = delete;
		node &operator= (const node &) = delete;

		virtual void complete() = 0;

	protected:
		~node() = default;

	private:
		// Protected by _mutex.
		frg::default_list_hook<node> _hook;
	};

public:
	void raise() {
		seq_.fetch_add(1, std::memory_order_acq_rel);

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

	template <typename Receiver>
	struct wait_operation final : private node {
		wait_operation(sequenced_event *evt, uint64_t in_seq, cancellation_token ct, Receiver r)
		: evt_{evt}, in_seq_{in_seq}, ct_{std::move(ct)}, r_{std::move(r)} { }

		bool start_inline() {
			uint64_t curr_seq = evt_->seq_.load(std::memory_order_release);
			uint64_t out_seq;
			bool completed = false;
			{
				frg::unique_lock lock(evt_->_mutex);

				if (curr_seq > in_seq_) {
					completed = true;
					out_seq = curr_seq;
				} else if (!cobs_.try_set(ct_)) {
					completed = true;
					out_seq = in_seq_;
				} else {
					evt_->queue_.push_back(this);
				}
			}

			if (completed) {
				execution::set_value_inline(r_, out_seq);
				return true;
			}

			return false;
		}

	private:
		void cancel() {
			uint64_t out_seq = evt_->seq_.load(std::memory_order_release);
			{
				frg::unique_lock lock(evt_->_mutex);

				if (out_seq <= in_seq_) {
					out_seq = in_seq_;
					auto it = evt_->queue_.iterator_to(this);
					evt_->queue_.erase(it);
				}
			}

			execution::set_value_noinline(r_, out_seq);
		}

		void complete() override {
			if (cobs_.try_reset())
				execution::set_value_noinline(r_, evt_->seq_.load(std::memory_order_release));
		}

		sequenced_event *evt_;
		uint64_t in_seq_;
		cancellation_token ct_;
		Receiver r_;
		cancellation_observer<frg::bound_mem_fn<&wait_operation::cancel>> cobs_{this};
	};

	struct wait_sender {
		using value_type = uint64_t;

		template <typename Receiver>
		friend wait_operation<Receiver> connect(wait_sender s, Receiver r) {
			return {s.evt, s.in_seq, s.ct, std::move(r)};
		}

		friend sender_awaiter<wait_sender, bool> operator co_await(wait_sender s) {
			return {s};
		}

		sequenced_event *evt;
		uint64_t in_seq;
		cancellation_token ct;
	};

	wait_sender async_wait(uint64_t in_seq, cancellation_token ct = {}) {
		return {this, in_seq, ct};
	}

private:
	platform::mutex _mutex;

	std::atomic_uint64_t seq_ = 0;

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
