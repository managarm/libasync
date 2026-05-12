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
	: allocator_{std::move(allocator)} {}

	~queue() {
		assert(sinks_.empty());
		while(!buffer_.empty())
			frg::destruct(allocator_, buffer_.pop_front());
	}

	queue(const queue &) = delete;
	queue &operator=(const queue &) = delete;

private:
	// Note: element is heap allocated. It must be allocated *outside* the mutex.
	struct element {
		template<typename... Ts>
		element(Ts&&... args)
		: item(std::forward<Ts>(args)...) {}

		T item;
		frg::default_list_hook<element> hook;
	};

	struct sink {
		friend struct queue;

	protected:
		virtual ~sink() = default;

	public:
		virtual void complete() = 0;

	protected:
		element *el{nullptr};

	private:
		frg::default_list_hook<sink> hook_;
	};

	bool try_cancel(sink *sp) {
		frg::unique_lock lock{mutex_};

		if(!sp->el) {
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
		auto *el = frg::construct<element>(allocator_, std::forward<Ts>(arg)...);

		sink *complete_sp = nullptr;
		{
			frg::unique_lock lock{mutex_};

			if(!sinks_.empty()) {
				assert(buffer_.empty());
				auto sp = sinks_.pop_front();
				sp->el = el;
				complete_sp = sp;
			}else{
				buffer_.push_back(el);
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
			element *el = nullptr;
			{
				frg::unique_lock lock{q_->mutex_};

				if(!q_->buffer_.empty()) {
					assert(q_->sinks_.empty());
					el = q_->buffer_.pop_front();
				}else{
					q_->sinks_.push_back(this);
				}
			}

			if(el) {
				frg::optional<T> result{std::move(el->item)};
				frg::destruct(q_->allocator_, el);
				return execution::set_value(r_, std::move(result));
			}
			cr_.listen(ct_);
		}

	private:
		struct try_cancel_fn {
			bool operator()(auto *cr) {
				auto self = frg::container_of(cr, &get_operation::cr_);
				return self->q_->try_cancel(self);
			}
		};
		struct resume_fn {
			void operator()(auto *cr) {
				auto self = frg::container_of(cr, &get_operation::cr_);
				frg::optional<T> result;
				if (self->el) {
					result = std::move(self->el->item);
					frg::destruct(self->q_->allocator_, self->el);
				}
				execution::set_value(self->r_, std::move(result));
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
		frg::unique_lock lock{mutex_};

		return buffer_.empty();
	}

	frg::optional<T> maybe_get() {
		element *el;
		{
			frg::unique_lock lock{mutex_};

			if(buffer_.empty())
				return frg::null_opt;
			el = buffer_.pop_front();
		}
		auto value = std::move(el->item);
		frg::destruct(allocator_, el);
		return value;
	}

private:
	platform::mutex mutex_;

	[[no_unique_address]] Allocator allocator_;

	frg::intrusive_list<
		element,
		frg::locate_member<
			element,
			frg::default_list_hook<element>,
			&element::hook
		>
	> buffer_;

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
