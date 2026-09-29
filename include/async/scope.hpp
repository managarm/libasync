#pragma once

#include <atomic>
#include <type_traits>

#include <async/basic.hpp>
#include <async/execution.hpp>
#include <frg/allocation.hpp>
#include <frg/list.hpp>

namespace async {

//---------------------------------------------------------------------------------------
// dynamic_scope() and dynamic_deferred_scope()
//---------------------------------------------------------------------------------------

template<bool Deferred, typename Receiver, typename Sender, typename Allocator, typename Fill>
struct dynamic_scope_operation {
private:
	struct child;

	struct receiver {
		void set_value() {
			auto self = child_->self_;
			frg::destruct(self->allocator_, child_);
			self->complete_one_();
		}

		auto get_env() {
			return execution::get_env(child_->self_->dr_);
		}

		child *child_;
	};

	struct no_hook { };

	struct child {
		child(dynamic_scope_operation *self, Sender s)
		: self_{self}, op_{execution::connect(std::move(s), receiver{this})} { }

		dynamic_scope_operation *self_;
		[[no_unique_address]] std::conditional_t<Deferred,
				frg::default_list_hook<child>, no_hook> hook_;
		execution::operation_t<Sender, receiver> op_;
	};

public:
	dynamic_scope_operation(Allocator allocator, Fill fill, Receiver dr)
	: allocator_{std::move(allocator)}, fill_{std::move(fill)}, dr_{std::move(dr)} { }

	dynamic_scope_operation(const dynamic_scope_operation &) = delete;

	dynamic_scope_operation &operator= (const dynamic_scope_operation &) = delete;

	void start() {
		if constexpr (Deferred) {
			frg::intrusive_list<
				child,
				frg::locate_member<
					child,
					frg::default_list_hook<child>,
					&child::hook_
				>
			> pending;
			size_t n = 0;

			auto spawn = [&] (Sender s) {
				pending.push_back(frg::construct<child>(allocator_, this, std::move(s)));
				++n;
			};
			fill_(spawn);

			// No child is running yet, so a single RMW is enough.
			ctr_.fetch_add(n, std::memory_order_relaxed);

			// Children can complete (and destroy themselves) inline, so unlink them first.
			while(!pending.empty())
				execution::start(pending.pop_front()->op_);
		} else {
			auto spawn = [&] (Sender s) {
				ctr_.fetch_add(1, std::memory_order_relaxed);
				execution::start(frg::construct<child>(allocator_, this, std::move(s))->op_);
			};
			fill_(spawn);
		}

		// Drop the reference that prevents completion while start() is still running.
		complete_one_();
	}

private:
	void complete_one_() {
		auto c = ctr_.fetch_sub(1, std::memory_order_acq_rel);
		assert(c > 0);
		if(c == 1)
			execution::set_value(dr_);
	}

	Allocator allocator_;
	Fill fill_;
	Receiver dr_;
	std::atomic<size_t> ctr_{1};
};

template<bool Deferred, typename Sender, typename Allocator, typename Fill>
struct [[nodiscard]] dynamic_scope_sender {
	using value_type = void;

	template<Receives<value_type> Receiver>
	friend dynamic_scope_operation<Deferred, Receiver, Sender, Allocator, Fill>
	connect(dynamic_scope_sender s, Receiver r) {
		return {std::move(s.allocator), std::move(s.fill), std::move(r)};
	}

	Allocator allocator;
	Fill fill;
};

// Calls fill(spawn). Each spawn(sender) call starts a child immediately.
// Completes once fill() has returned and all children have completed.
template<Sender S, typename Allocator, typename Fill>
requires std::is_same_v<typename S::value_type, void>
dynamic_scope_sender<false, S, Allocator, Fill> dynamic_scope(Allocator allocator, Fill fill) {
	return {std::move(allocator), std::move(fill)};
}

// Like dynamic_scope() but children are only started after fill() returns.
template<Sender S, typename Allocator, typename Fill>
requires std::is_same_v<typename S::value_type, void>
dynamic_scope_sender<true, S, Allocator, Fill> dynamic_deferred_scope(Allocator allocator, Fill fill) {
	return {std::move(allocator), std::move(fill)};
}

template<bool Deferred, typename S, typename Allocator, typename Fill>
sender_awaiter<dynamic_scope_sender<Deferred, S, Allocator, Fill>>
operator co_await(dynamic_scope_sender<Deferred, S, Allocator, Fill> s) {
	return {std::move(s)};
}

} // namespace async
