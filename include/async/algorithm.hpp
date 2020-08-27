#pragma once

#include <frg/manual_box.hpp>
#include <frg/tuple.hpp>
#include <async/basic.hpp>
#include <async/cancellation.hpp>

namespace async {

template<typename Sender, typename Receiver>
struct connect_helper {
	using operation = execution::operation_t<Sender, Receiver>;

	operator operation () {
		return connect(std::move(s), std::move(r));
	}

	Sender s;
	Receiver r;
};

template<typename Sender, typename Receiver>
connect_helper<Sender, Receiver> make_connect_helper(Sender s, Receiver r) {
	return {std::move(s), std::move(r)};
}

//---------------------------------------------------------------------------------------
// transform()
//---------------------------------------------------------------------------------------

template<typename Receiver, typename F>
struct value_transform_receiver {
	value_transform_receiver(Receiver dr, F f)
	: dr_{std::move(dr)}, f_{std::move(f)} { }

	template<typename X>
	void set_value(X value) {
		execution::set_value(dr_, f_(std::move(value)));
	}

private:
	Receiver dr_; // Downstream receiver.
	[[no_unique_address]] F f_;
};

template<typename Receiver, typename F>
struct void_transform_receiver {
	void_transform_receiver(Receiver dr, F f)
	: dr_{std::move(dr)}, f_{std::move(f)} { }

	void set_value() {
		f_();
		execution::set_value(dr_);
	}

private:
	Receiver dr_; // Downstream receiver.
	[[no_unique_address]] F f_;
};

template<typename Sender, typename F>
struct [[nodiscard]] transform_sender;

template<typename Sender, typename F>
requires (!std::is_same_v<typename Sender::value_type, void>)
struct [[nodiscard]] transform_sender<Sender, F> {
	using value_type = std::invoke_result_t<F, typename Sender::value_type>;

	template<typename Receiver>
	friend auto connect(transform_sender s, Receiver dr) {
		return execution::connect(std::move(s.ds),
				value_transform_receiver<Receiver, F>{std::move(dr), std::move(s.f)});
	}

	sender_awaiter<transform_sender, value_type> operator co_await () {
		return {std::move(*this)};
	}

	Sender ds; // Downstream sender.
	F f;
};

template<typename Sender, typename F>
requires std::is_same_v<typename Sender::value_type, void>
struct [[nodiscard]] transform_sender<Sender, F> {
	using value_type = std::invoke_result_t<F>;

	template<typename Receiver>
	friend auto connect(transform_sender s, Receiver dr) {
		return execution::connect(std::move(s.ds),
				void_transform_receiver<Receiver, F>{std::move(dr), std::move(s.f)});
	}

	sender_awaiter<transform_sender, value_type> operator co_await () {
		return {std::move(*this)};
	}

	Sender ds; // Downstream sender.
	F f;
};

template<typename Sender, typename F>
transform_sender<Sender, F> transform(Sender ds, F f) {
	return {std::move(ds), std::move(f)};
}

//---------------------------------------------------------------------------------------
// repeat_while()
//---------------------------------------------------------------------------------------

template<typename C, typename SF, typename R>
struct [[nodiscard]] repeat_while_operation {
	using sender_type = std::invoke_result_t<SF>;

	repeat_while_operation(C cond, SF factory, R dr)
	: cond_{std::move(cond)}, factory_{std::move(factory)}, dr_{std::move(dr)} { }

	repeat_while_operation(const repeat_while_operation &) = delete;

	repeat_while_operation &operator=(const repeat_while_operation &) = delete;

	bool start_inline() {
		if(loop_()) {
			execution::set_value_inline(dr_);
			return true;
		}

		return false;
	}

private:
	// Returns true if repeat_while() completes.
	bool loop_() {
		while(cond_()) {
			box_.construct_with([&] {
				return execution::connect(factory_(), receiver{this});
			});
			if(!execution::start_inline(*box_))
				return false;
			box_.destruct();
		}

		return true;
	}

	struct receiver {
		receiver(repeat_while_operation *self)
		: self_{self} { }

		void set_value_inline() {
			// Do nothing.
		}

		void set_value_noinline() {
			auto s = self_; // box_.destruct() will destruct this.
			s->box_.destruct();
			if(s->loop_())
				execution::set_value_noinline(s->dr_);
		}

	private:
		repeat_while_operation *self_;
	};

	C cond_;
	SF factory_;
	R dr_; // Downstream receiver.
	frg::manual_box<execution::operation_t<sender_type, receiver>> box_;
};

template<typename C, typename SF>
struct repeat_while_sender {
	using value_type = void;

	sender_awaiter<repeat_while_sender> operator co_await() {
		return {std::move(*this)};
	}

	template<typename R>
	repeat_while_operation<C, SF, R> connect(R receiver) {
		return {std::move(cond), std::move(factory), std::move(receiver)};
	}

	C cond;
	SF factory;
};

template<typename C, typename SF>
repeat_while_sender<C, SF> repeat_while(C cond, SF factory) {
	return {std::move(cond), std::move(factory)};
}

//---------------------------------------------------------------------------------------
// race_and_cancel()
//---------------------------------------------------------------------------------------

template<typename Receiver, typename Tuple, typename S>
struct race_and_cancel_operation;

template<typename... Functors>
struct race_and_cancel_sender {
	using value_type = void;

	template<typename Receiver>
	friend race_and_cancel_operation<Receiver, frg::tuple<Functors...>,
			std::index_sequence_for<Functors...>>
	connect(race_and_cancel_sender s, Receiver r) {
		return {std::move(s), std::move(r)};
	}

	frg::tuple<Functors...> fs;
};

template<typename Receiver, typename... Functors, size_t... Is>
struct race_and_cancel_operation<Receiver, frg::tuple<Functors...>, std::index_sequence<Is...>> {
private:
	using functor_tuple = frg::tuple<Functors...>;

	template<size_t I>
	struct internal_receiver {
		internal_receiver(race_and_cancel_operation *self)
		: self_{self} { }

		void set_value() {
			auto n = self_->n_done_.fetch_add(1, std::memory_order_acq_rel);
			if(!n) {
				for(unsigned int j = 0; j < sizeof...(Is); ++j)
					if(j != I)
						self_->cs_[j].cancel();
			}
			if(n + 1 == sizeof...(Is))
				execution::set_value(self_->r_);
		}

	private:
		race_and_cancel_operation *self_;
	};

	template<size_t I>
	using internal_sender = std::invoke_result_t<
		typename std::tuple_element<I, functor_tuple>::type,
		cancellation_token
	>;

	template<size_t I>
	using internal_operation = execution::operation_t<internal_sender<I>, internal_receiver<I>>;

	using operation_tuple = frg::tuple<internal_operation<Is>...>;

	auto make_operations_tuple(race_and_cancel_sender<Functors...> s) {
		return frg::make_tuple(
			make_connect_helper(
				(s.fs.template get<Is>())(cancellation_token{cs_[Is]}),
				internal_receiver<Is>{this}
			)...
		);
	}

public:
	race_and_cancel_operation(race_and_cancel_sender<Functors...> s, Receiver r)
	: r_{std::move(r)}, ops_{make_operations_tuple(std::move(s))}, n_done_{0} { }

	void start() {
		(execution::start(ops_.template get<Is>()), ...);
	}

private:
	Receiver r_;
	operation_tuple ops_;
	cancellation_event cs_[sizeof...(Is)];
	std::atomic<unsigned int> n_done_;
};

template<typename... Functors>
sender_awaiter<race_and_cancel_sender<Functors...>>
operator co_await(race_and_cancel_sender<Functors...> s) {
	return {std::move(s)};
}

template<typename... Functors>
race_and_cancel_sender<Functors...> race_and_cancel(Functors... fs) {
	return {{fs...}};
}

//---------------------------------------------------------------------------------------
// let()
//---------------------------------------------------------------------------------------

template <typename Receiver, typename Pred, typename Func>
struct let_operation {
	using imm_type = std::invoke_result_t<Pred>;
	using sender_type = std::invoke_result_t<Func, std::add_lvalue_reference_t<imm_type>>;
	using value_type = typename sender_type::value_type;

	let_operation(Pred pred, Func func, Receiver r)
	: pred_{std::move(pred)}, func_{std::move(func)}, imm_{}, r_{std::move(r)} { }

	let_operation(const let_operation &) = delete;
	let_operation &operator=(const let_operation &) = delete;

	~let_operation() {
		op_.destruct();
	}

public:
	void start() {
		imm_ = std::move(pred_());
		op_.construct_with([&]{ return execution::connect(func_(imm_), std::move(r_)); });
		op_->start();
	}

private:
	Pred pred_;
	Func func_;
	imm_type imm_;
	Receiver r_;
	frg::manual_box<execution::operation_t<sender_type, Receiver>> op_;
};

template <typename Pred, typename Func>
struct [[nodiscard]] let_sender {
	using imm_type = std::invoke_result_t<Pred>;
	using value_type = typename std::invoke_result_t<Func, std::add_lvalue_reference_t<imm_type>>::value_type;

	template<typename Receiver>
	friend let_operation<Receiver, Pred, Func>
	connect(let_sender s, Receiver r) {
		return {std::move(s.pred), std::move(s.func), std::move(r)};
	}

	Pred pred;
	Func func;
};

template <typename Pred, typename Func>
sender_awaiter<let_sender<Pred, Func>, typename let_sender<Pred, Func>::value_type>
operator co_await(let_sender<Pred, Func> s) {
	return {std::move(s)};
}

template <typename Pred, typename Func>
let_sender<Pred, Func> let(Pred pred, Func func) {
	return {std::move(pred), std::move(func)};
}

} // namespace async
