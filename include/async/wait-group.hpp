#pragma once

/* refactored from oneshot_event */

#include <async/execution.hpp>
#include <atomic>
#include <async/algorithm.hpp>
#include <async/cancellation.hpp>
#include <frg/container_of.hpp>
#include <frg/list.hpp>

namespace async {

struct wait_group {
private:
	struct node {
		friend struct wait_group;

		node() = default;

		node(const node &) = delete;

		node &operator= (const node &) = delete;

		virtual void complete() = 0;

		bool was_cancelled() const { return cancelled_; }

	protected:
		virtual ~node() = default;

	private:
		// Protected by mutex_.
		frg::default_list_hook<node> _hook;
		bool cancelled_ = false;
	};

public:
	wait_group(size_t ctr) : ctr_ { ctr } {}

	void done() {
		// Grab all items and mark them as retired while we hold the lock.
		frg::intrusive_list<
			node,
			frg::locate_member<
				node,
				frg::default_list_hook<node>,
				&node::_hook
			>
		> items;

		// We can decrement outside of the lock as long as we do not reach zero.
		auto v = ctr_.load(std::memory_order_relaxed);
		while (v > 1) {
			if (ctr_.compare_exchange_strong(v, v - 1, std::memory_order_acq_rel, std::memory_order_relaxed))
				return;
		}
		assert(v > 0);

		{
			frg::unique_lock lock(mutex_);
			// Only wake waiters if we reach zero.
			if (ctr_.fetch_sub(1, std::memory_order_acq_rel) > 1)
				return;
			items.splice(items.end(), queue_);
		}

		// Now invoke the individual callbacks.
		while(!items.empty()) {
			auto item = items.front();
			items.pop_front();
			item->complete();
		}
	}

	bool try_cancel(node *nd) {
		frg::unique_lock lock(mutex_);

		// Relaxed since non-zero -> zero transitions cannot happen while the mutex is held.
		if(ctr_.load(std::memory_order_relaxed) > 0) {
			nd->cancelled_ = true;
			auto it = queue_.iterator_to(nd);
			queue_.erase(it);
			return true;
		}
		return false;
	}

	void add(size_t new_members) {
		ctr_.fetch_add(new_members, std::memory_order_acq_rel);
	}

	// ----------------------------------------------------------------------------------
	// wait() and its boilerplate.
	// ----------------------------------------------------------------------------------

	template<typename Receiver>
	struct wait_operation final : private node {
		wait_operation(wait_group *wg, cancellation_token ct, Receiver r)
		: wg_{wg}, ct_{std::move(ct)}, r_{std::move(r)} { }

		void start() {
			bool fast_path = false;
			{
				frg::unique_lock lock(wg_->mutex_);

				// Relaxed since non-zero -> zero transitions cannot happen while the mutex is held.
				if(wg_->ctr_.load(std::memory_order_relaxed) > 0) {
					wg_->queue_.push_back(this);
				}else{
					fast_path = true;
				}
			}

			if(fast_path)
				return execution::set_value(r_, true);
			cr_.listen(ct_);
		}

	private:
		struct try_cancel_fn {
			bool operator()(auto *cr) {
				auto self = frg::container_of(cr, &wait_operation::cr_);
				return self->wg_->try_cancel(self);
			}
		};
		struct resume_fn {
			void operator()(auto *cr) {
				auto self = frg::container_of(cr, &wait_operation::cr_);
				execution::set_value(self->r_, !self->was_cancelled());
			}
		};

		void complete() override {
			cr_.complete();
		}

		wait_group *wg_;
		cancellation_token ct_;
		Receiver r_;
		cancellation_resolver<try_cancel_fn, resume_fn> cr_;
	};

	struct [[nodiscard]] wait_sender {
		using value_type = bool;

		template<typename Receiver>
		friend wait_operation<Receiver> connect(wait_sender s, Receiver r) {
			return {s.wg, s.ct, std::move(r)};
		}

		sender_awaiter<wait_sender, bool> operator co_await () {
			return {*this};
		}

		wait_group *wg;
		cancellation_token ct;
	};

	wait_sender wait(cancellation_token ct) {
		return {this, ct};
	}

	auto wait() {
		return async::transform(wait(cancellation_token{}), [] (bool waitSuccess) {
			assert(waitSuccess);
		});
	}

	/* BasicLockable support */
	void lock() { add(1); }
	void unlock() { done(); }
private:
	platform::mutex mutex_;

	std::atomic<size_t> ctr_;

	frg::intrusive_list<
		node,
		frg::locate_member<
			node,
			frg::default_list_hook<node>,
			&node::_hook
		>
	> queue_;
};

namespace wait_in_group_details_ {
template<typename OriginalS>
struct [[nodiscard]] sender_ {
	using value_type = typename OriginalS::value_type;
	wait_group &wg_;
	OriginalS originals_;

	template<typename OriginalR>
	struct operation_ {
		OriginalR originalr_;
		wait_group &wg_;

		struct receiver_ {
			operation_ &op_;

			template<typename... Ts>
			requires(sizeof...(Ts) <= 1)
			void set_value(Ts &&...ts) {
				op_.wg_.done();
				execution::set_value(
					op_.originalr_,
					std::forward<Ts>(ts)...
				);
			}

			auto get_env() {
				return execution::get_env(op_.originalr_);
			}
		};

		execution::operation_t<OriginalS, receiver_> originalop_;

		operation_(sender_ s, OriginalR r)
			: originalr_(std::move(r))
			, wg_(s.wg_)
			, originalop_(execution::connect(std::move(s.originals_), receiver_{*this}))
		{}

		void start() {
			wg_.add(1);
			return execution::start(originalop_);
		}

		operation_(const operation_&) = delete;
		operation_ &operator =(const operation_&) = delete;
	};

	async::sender_awaiter<sender_, typename OriginalS::value_type>
	friend operator co_await(sender_ &&s) {
		return { std::move(s) };
	}
};
template<typename OS, typename R>
auto connect(sender_<OS> s, R r) {
	using op = typename sender_<OS>::template operation_<R>;
	return op { std::move(s), std::move(r) };
}
} // namespace wait_in_group_details_

template<typename Original>
wait_in_group_details_::sender_<std::remove_reference_t<Original>>
wait_in_group(wait_group &wg, Original &&to_wrap) {
	return {wg, std::forward<Original>(to_wrap)};
}

} // namespace async
