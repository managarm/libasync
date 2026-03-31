#pragma once

#include <async/basic.hpp>
#include <async/cancellation.hpp>
#include <frg/container_of.hpp>
#include <frg/list.hpp>
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
		virtual void complete() = 0;

	protected:
		frg::optional<T> value;

	private:
		frg::default_list_hook<sink> hook_;
	};

	bool try_cancel(sink *sp) {
		frg::unique_lock lock{mutex_};

		if(!sp->value) {
			auto it = sinks_.iterator_to(sp);
			sinks_.erase(it);
			return true;
		}
		return false;
	}

public:
	void put(T item) {
		emplace(std::move(item));
	}

	template<typename... Ts>
	void emplace(Ts&&... arg) {
		sink *complete_sp = nullptr;
		{
			frg::unique_lock lock{mutex_};

			if(!sinks_.empty()) {
				assert(buffer_.empty());
				auto sp = sinks_.pop_front();
				sp->value.emplace(std::forward<Ts>(arg)...);
				complete_sp = sp;
			}else{
				buffer_.emplace_back(std::forward<Ts>(arg)...);
			}
		}

		if(complete_sp)
			complete_sp->complete();
	}

	// ----------------------------------------------------------------------------------
	// async_get() and its boilerplate.
	// ----------------------------------------------------------------------------------

	template<typename Receiver>
	struct get_operation final : private sink {
		get_operation(queue *q, cancellation_token ct, Receiver r)
		: q_{q}, ct_{std::move(ct)}, r_{std::move(r)} { }

		void start() {
			bool fast_path = false;
			{
				frg::unique_lock lock{q_->mutex_};

				if(!q_->buffer_.empty()) {
					assert(q_->sinks_.empty());
					value = std::move(q_->buffer_.front());
					q_->buffer_.pop_front();
					fast_path = true;
				}else{
					q_->sinks_.push_back(this);
				}
			}

			if(fast_path)
				return execution::set_value(r_, std::move(value));
			cr_.listen(ct_);
		}

	private:
		using sink::value;

		struct try_cancel_fn {
			bool operator()(auto *cr) {
				auto self = frg::container_of(cr, &get_operation::cr_);
				return self->q_->try_cancel(self);
			}
		};
		struct resume_fn {
			void operator()(auto *cr) {
				auto self = frg::container_of(cr, &get_operation::cr_);
				execution::set_value(self->r_, std::move(self->value));
			}
		};

		void complete() override {
			cr_.complete();
		}

		queue *q_;
		cancellation_token ct_;
		Receiver r_;
		cancellation_resolver<try_cancel_fn, resume_fn> cr_;
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
