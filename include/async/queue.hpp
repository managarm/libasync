#pragma once

#include <async/basic.hpp>
#include <async/cancellation.hpp>
#include <frg/list.hpp>
#include <frg/functional.hpp>
#include <frg/optional.hpp>

namespace async {

template<typename T, typename Allocator>
struct queue {
	queue(Allocator allocator = {})
	: buffer_{allocator} {}

private:
	struct sink {
		friend struct queue;

	protected:
		virtual ~sink() = default;

	public:
		virtual void cancel() = 0;
		virtual void complete() = 0;

	protected:
		cancellation_observer<frg::bound_mem_fn<&sink::cancel>> cobs{this};
		frg::optional<T> value;

	private:
		frg::default_list_hook<sink> hook_;
	};

public:
	void put(T item) {
		emplace(std::move(item));
	}

	template<typename... Ts>
	void emplace(Ts&&... arg) {
		sink *retire_sp = nullptr;
		{
			frg::unique_lock lock{mutex_};

			if(!sinks_.empty()) {
				assert(buffer_.empty());
				auto sp = sinks_.pop_front();
				sp->value.emplace(std::forward<Ts>(arg)...);
				if(sp->cobs.try_reset())
					retire_sp = sp;
			}else{
				buffer_.emplace_back(std::forward<Ts>(arg)...);
			}
		}

		if(retire_sp)
			retire_sp->complete();
	}

	// ----------------------------------------------------------------------------------
	// async_get() and its boilerplate.
	// ----------------------------------------------------------------------------------

	template<typename Receiver>
	struct get_operation final : private sink {
		get_operation(queue *q, cancellation_token ct, Receiver r)
		: q_{q}, ct_{std::move(ct)}, r_{std::move(r)} { }

		void start() {
			bool retire = false;
			{
				frg::unique_lock lock{q_->mutex_};

				if(!q_->buffer_.empty()) {
					assert(q_->sinks_.empty());
					value = std::move(q_->buffer_.front());
					q_->buffer_.pop_front();
					retire = true;
				}else{
					if(!cobs.try_set(ct_)) {
						retire = true;
					}else{
						q_->sinks_.push_back(this);
					}
				}
			}

			if(retire)
				return execution::set_value(r_, std::move(value));
		}

	private:
		using sink::cobs;
		using sink::value;

		void cancel() override {
			{
				frg::unique_lock lock{q_->mutex_};

				// We either have a value, or we are not part of the list anymore.
				if(!value) {
					auto it = q_->sinks_.iterator_to(this);
					q_->sinks_.erase(it);
				}
			}

			execution::set_value(r_, std::move(value));
		}

		void complete() override {
			execution::set_value(r_, std::move(value));
		}

		queue *q_;
		cancellation_token ct_;
		Receiver r_;
	};

	struct get_sender {
		using value_type = frg::optional<T>;

		template<typename Receiver>
		friend get_operation<Receiver> connect(get_sender s, Receiver r) {
			return {s.q, s.ct, std::move(r)};
		}

		friend sender_awaiter<get_sender, frg::optional<T>> operator co_await (get_sender s) {
			return {s};
		}

		queue *q;
		cancellation_token ct;
	};

	get_sender async_get(cancellation_token ct = {}) {
		return {this, ct};
	}

	bool empty() {
		return buffer_.empty();
	}

	frg::optional<T> maybe_get() {
		frg::unique_lock lock{mutex_};

		if(buffer_.empty())
			return {};
		auto object = std::move(buffer_.front());
		buffer_.pop_front();
		return object;
	}

private:
	platform::mutex mutex_;

	frg::list<T, Allocator> buffer_;

	frg::intrusive_list<
		sink,
		frg::locate_member<
			sink,
			frg::default_list_hook<sink>,
			&sink::hook_
		>
	> sinks_;
};

} // namespace async
