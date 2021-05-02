#pragma once

#include <algorithm>

#include <async/cancellation.hpp>
#include <frg/functional.hpp>
#include <frg/list.hpp>

namespace async {

template<typename T>
struct post_ack_handle;

template<typename T>
struct post_ack_agent;

template<typename T>
struct post_ack_mechanism {
private:
	friend struct post_ack_handle<T>;
	friend struct post_ack_agent<T>;

/*
	struct state {
		none,
		submitted,
		pending,
		retired
	};
*/

	struct node {
		virtual void complete() = 0;

		uint64_t node_seq;
		std::atomic<unsigned int> acks_left;
		// These following fields are protected by the mechanism's mutex_.
		frg::default_list_hook<node> hook;
		T object;

	protected:
		~node() = default;
	};

	struct poll_node {
		virtual void complete() = 0;

		// These fields are protected by the mechanism's mutex_.
		bool pending = false;
		node *nd = nullptr;
		frg::default_list_hook<poll_node> hook;

	protected:
		~poll_node() = default;
	};

public:
	template<typename R>
	struct [[nodiscard]] post_operation final : private node {
		post_operation(post_ack_mechanism *mech, T object, R receiver)
		: mech_{mech}, receiver_{std::move(receiver)} {
			node::object = std::move(object);
		}

		bool start_inline() {
			frg::intrusive_list<
				poll_node,
				frg::locate_member<
					poll_node,
					frg::default_list_hook<poll_node>,
					&poll_node::hook
				>
			> poll_pending;
			{
				frg::unique_lock lock(mech_->mutex_);

				node::node_seq = mech_->post_seq_++;

				if(!mech_->active_agents_) {
					assert(mech_->poll_queue_.empty()); // Otherwise, the should be an agent.
					execution::set_value_noinline(receiver_);
					return true;
				}

				node::acks_left.store(mech_->active_agents_, std::memory_order_relaxed);
				mech_->queue_.push_back(this);
				while(!mech_->poll_queue_.empty()) {
					auto pn = mech_->poll_queue_.pop_front();
					assert(!pn->pending);
					assert(!pn->nd);
					pn->pending = true;
					pn->nd = this;
					poll_pending.push_back(pn);
				}
			}

			while(!poll_pending.empty()) {
				auto pn = poll_pending.pop_front();
				pn->complete();
			}
			return false;
		}

	private:
		void complete() override {
			execution::set_value_noinline(receiver_);
		}

		post_ack_mechanism *mech_;
		R receiver_;
	};

	struct [[nodiscard]] post_sender {
		friend sender_awaiter<post_sender> operator co_await (post_sender sender) {
			return {sender};
		}

		template<typename R>
		post_operation<R> connect(R receiver) {
			return {mech, std::move(object), std::move(receiver)};
		}

		post_ack_mechanism *mech;
		T object;
	};

	post_sender post(T object) {
		return {this, std::move(object)};
	}

private:
	platform::mutex mutex_;

	uint64_t post_seq_ = 0;

	unsigned int active_agents_ = 0;

	frg::intrusive_list<
		node,
		frg::locate_member<
			node,
			frg::default_list_hook<node>,
			&node::hook
		>
	> queue_;

	frg::intrusive_list<
		poll_node,
		frg::locate_member<
			poll_node,
			frg::default_list_hook<poll_node>,
			&poll_node::hook
		>
	> poll_queue_;
};

template<typename T>
struct post_ack_handle {
private:
	using node = typename post_ack_mechanism<T>::node;

public:
	friend void swap(post_ack_handle &lhs, post_ack_handle &rhs) {
		using std::swap;
		swap(lhs.mech_, rhs.mech_);
		swap(lhs.nd_, rhs.nd_);
		swap(lhs.acked_, rhs.acked_);
	}

	explicit post_ack_handle() = default;

	explicit post_ack_handle(post_ack_mechanism<T> *mech, node *nd)
	: mech_{mech}, nd_{nd} { }

	post_ack_handle(const post_ack_handle &other) = delete;

	post_ack_handle(post_ack_handle &&other)
	: post_ack_handle() {
		swap(*this, other);
	}

	~post_ack_handle() {
		assert(!nd_ || acked_);
	}

	post_ack_handle &operator= (post_ack_handle other) {
		swap(*this, other);
		return *this;
	}

	void ack() {
		assert(nd_);
		assert(!acked_);

		auto n = nd_->acks_left.fetch_sub(1, std::memory_order_acq_rel);
		assert(n >= 1);
		if(n == 1) {
			{
				frg::unique_lock lock(mech_->mutex_);

				mech_->queue_.erase(mech_->queue_.iterator_to(nd_));
			}
			nd_->complete();
		}
		acked_ = true;
	}

	explicit operator bool () {
		return static_cast<bool>(nd_);
	}

	T *operator-> () {
		assert(nd_);
		return &nd_->object;
	}

	T &operator* () {
		assert(nd_);
		return nd_->object;
	}

private:
	post_ack_mechanism<T> *mech_ = nullptr;
	node *nd_ = nullptr;
	bool acked_ = false;
};

template<typename T>
struct post_ack_agent {
private:
	using node = typename post_ack_mechanism<T>::node;
	using poll_node = typename post_ack_mechanism<T>::poll_node;

public:
	post_ack_agent() = default;

	~post_ack_agent() {
		assert(!mech_);
	}

	void attach(post_ack_mechanism<T> *mech) {
		assert(!mech_);
		mech_ = mech;

		{
			frg::unique_lock lock(mech_->mutex_);

			poll_seq_ = mech_->post_seq_;
			++mech_->active_agents_;
		}
	}

	void detach() {
		assert(mech_);

		{
			frg::unique_lock lock(mech_->mutex_);

			--mech_->active_agents_;
			auto retire_seq = mech_->post_seq_;

			while(retire_seq > poll_seq_) {
				auto it = std::find_if(mech_->queue_.begin(), mech_->queue_.end(),
						[&] (auto cand) { return cand->node_seq == poll_seq_; });
				assert(it != mech_->queue_.end());
				auto nd = *it;

				auto n = nd->acks_left.fetch_sub(1, std::memory_order_acq_rel);
				assert(n >= 1);
				if(n == 1)
					mech_->queue_.erase(mech_->queue_.iterator_to(nd));

				// Run the completion handler without locks.
				lock.unlock();
				nd->complete();

				++poll_seq_;
				if(retire_seq == poll_seq_) // Avoid re-locking.
					break;
				lock.lock();
			}
		}

		mech_ = nullptr;
	}

	template<typename R>
	struct [[nodiscard]] poll_operation final : private poll_node {
	private:
		using poll_node::pending;
		using poll_node::nd;

	public:
		poll_operation(post_ack_agent *agnt, cancellation_token ct, R receiver)
		: agnt_{agnt}, ct_{ct}, receiver_{std::move(receiver)} { }

		void start() {
			assert(agnt_->mech_);

			auto seq = agnt_->poll_seq_++;

			{
				frg::unique_lock lock(agnt_->mech_->mutex_);
				assert(!nd);

				if(agnt_->mech_->post_seq_ > seq) {
					// Fast path: successful completion.
					auto it = std::find_if(
							agnt_->mech_->queue_.begin(),
							agnt_->mech_->queue_.end(),
							[&] (auto cand) { return cand->node_seq == seq; });
					assert(it != agnt_->mech_->queue_.end());
					pending = true;
					nd = *it;
				}else if(!cobs_.try_set(ct_)) {
					// Fast path: cancellation.
					pending = true;
				}else{
					// Slow path.
					agnt_->mech_->poll_queue_.push_back(this);
					return;
				}
			}

			execution::set_value(receiver_, post_ack_handle<T>{agnt_->mech_, nd});
		}

	private:
		void complete() override {
			if(cobs_.try_reset())
				execution::set_value(receiver_, post_ack_handle<T>{agnt_->mech_, nd});
		}

		void complete_cancel() {
			{
				frg::unique_lock lock(agnt_->mech_->mutex_);

				if(!pending) {
					assert(!nd);
					pending = true;
					agnt_->mech_->poll_queue_.erase(agnt_->mech_->poll_queue_.iterator_to(this));
				}
			}

			if(nd)
				execution::set_value(receiver_, post_ack_handle<T>{agnt_->mech_, nd});
			else
				execution::set_value(receiver_, post_ack_handle<T>{});
		}

		post_ack_agent *agnt_;
		cancellation_token ct_;
		R receiver_;
		cancellation_observer<frg::bound_mem_fn<&poll_operation::complete_cancel>> cobs_{this};
	};

	struct [[nodiscard]] poll_sender {
		using value_type = post_ack_handle<T>;

		friend sender_awaiter<poll_sender, post_ack_handle<T>>
		operator co_await (poll_sender sender) {
			return {sender};
		}

		template<typename R>
		poll_operation<R> connect(R receiver) {
			return {agnt, std::move(ct), std::move(receiver)};
		}

		post_ack_agent *agnt;
		cancellation_token ct;
	};

	poll_sender poll(cancellation_token ct = {}) {
		return {this, std::move(ct)};
	}

private:
	post_ack_mechanism<T> *mech_ = nullptr;
	uint64_t poll_seq_;
};

} // namespace async
