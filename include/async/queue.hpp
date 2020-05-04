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
		~sink() = default;

	public:
		virtual void consume(T object) = 0;

	private:
		frg::default_list_hook<sink> hook_;
	};

public:
	void put(T item) {
		emplace(std::move(item));
	}

	template<typename... Ts>
	void emplace(Ts&&... arg) {
		if(!sinks_.empty()) {
			assert(buffer_.empty());
			auto sp = sinks_.pop_front();
			sp->consume(T(std::forward<Ts>(arg)...));
		}else{
			buffer_.emplace_back(std::forward<Ts>(arg)...);
		}
	}

	// ----------------------------------------------------------------------------------
	// async_get() and its boilerplate.
	// ----------------------------------------------------------------------------------

	template<typename Receiver>
	struct get_operation final : private sink {
		get_operation(queue *q, cancellation_token ct, Receiver r)
		: q_{q}, ct_{std::move(ct)}, r_{std::move(r)}, cobs_{this} { }

		void start() {
			if(!q_->buffer_.empty()) {
				assert(q_->sinks_.empty());
				auto object = std::move(q_->buffer_.front());
				q_->buffer_.pop_front();
				r_.set_value(std::move(object));
			}else{
				if(!cobs_.try_set(ct_)) {
					r_.set_value(frg::null_opt);
				}else{
					q_->sinks_.push_back(this);
				}
			}
		}

	private:
		void cancel() {
			if(value_) {
				r_.set_value(std::move(value_));
			}else{
				auto it = q_->sinks_.iterator_to(this);
				q_->sinks_.erase(it);
				r_.set_value(frg::null_opt);
			}
		}

		void consume(T object) override {
			if(cobs_.try_reset()) {
				r_.set_value(std::move(object));
			}else{
				value_ = std::move(object);
			}
		}

		queue *q_;
		cancellation_token ct_;
		Receiver r_;
		cancellation_observer<frg::bound_mem_fn<&get_operation::cancel>> cobs_;
		frg::optional<T> value_;
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

private:
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
