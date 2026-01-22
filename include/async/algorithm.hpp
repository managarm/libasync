#pragma once

#include <algorithm>
#include <concepts>

#include <async/basic.hpp>
#include <async/cancellation.hpp>
#include <frg/manual_box.hpp>
#include <frg/tuple.hpp>

namespace async {

template<Sender Sender, Receives<typename Sender::value_type> Receiver>
struct connect_helper {
	using operation = execution::operation_t<Sender, Receiver>;

	operator operation () {
		return execution::connect(std::move(s), std::move(r));
	}

	Sender s;
	Receiver r;
};

template<Sender Sender, Receives<typename Sender::value_type> Receiver>
connect_helper<Sender, Receiver> make_connect_helper(Sender s, Receiver r) {
	return {std::move(s), std::move(r)};
}

//---------------------------------------------------------------------------------------
// invocable()
//---------------------------------------------------------------------------------------

template<typename F, typename R>
struct [[nodiscard]] invocable_operation {
	invocable_operation(F f, R r)
	: f_{std::move(f)}, r_{std::move(r)} { }

	invocable_operation(const invocable_operation &) = delete;

	invocable_operation &operator= (const invocable_operation &) = delete;

	bool start_inline() {
		if constexpr (std::is_same_v<std::invoke_result_t<F>, void>) {
			f_();
			execution::set_value_inline(r_);
		}else{
			execution::set_value_inline(r_, f_());
		}
		return true;
	}

private:
	F f_;
	R r_;
};

template<typename F>
struct [[nodiscard]] invocable_sender {
	using value_type = std::invoke_result_t<F>;

	template<Receives<value_type> R>
	invocable_operation<F, R> connect(R r) {
		return {std::move(f), std::move(r)};
	}

	sender_awaiter<F, value_type> operator co_await() {
		return {*this};
	}

	F f;
};

template<typename F>
invocable_sender<F> invocable(F f) {
	return {std::move(f)};
}

//---------------------------------------------------------------------------------------
// transform()
//---------------------------------------------------------------------------------------

template<typename Receiver, typename F>
struct value_transform_receiver {
	value_transform_receiver(Receiver dr, F f)
	: dr_{std::move(dr)}, f_{std::move(f)} { }

	template<typename X>
	void set_value_inline(X value) {
		if constexpr (std::is_same_v<std::invoke_result_t<F, X>, void>) {
			f_(std::move(value));
			execution::set_value_inline(dr_);
		}else{
			execution::set_value_inline(dr_, f_(std::move(value)));
		}
	}

	template<typename X>
	void set_value_noinline(X value) {
		if constexpr (std::is_same_v<std::invoke_result_t<F, X>, void>) {
			f_(std::move(value));
			execution::set_value_noinline(dr_);
		}else{
			execution::set_value_noinline(dr_, f_(std::move(value)));
		}
	}

private:
	Receiver dr_; // Downstream receiver.
	[[no_unique_address]] F f_;
};

template<typename Receiver, typename F>
struct void_transform_receiver {
	void_transform_receiver(Receiver dr, F f)
	: dr_{std::move(dr)}, f_{std::move(f)} { }

	void set_value_inline() {
		if constexpr (std::is_same_v<std::invoke_result_t<F>, void>) {
			f_();
			execution::set_value_inline(dr_);
		}else{
			execution::set_value_inline(dr_, f_());
		}
	}

	void set_value_noinline() {
		if constexpr (std::is_same_v<std::invoke_result_t<F>, void>) {
			f_();
			execution::set_value_noinline(dr_);
		}else{
			execution::set_value_noinline(dr_, f_());
		}
	}

private:
	Receiver dr_; // Downstream receiver.
	[[no_unique_address]] F f_;
};

template<typename Sender, typename F>
struct [[nodiscard]] transform_sender;

template<typename Sender, typename F>
requires (!std::same_as<typename Sender::value_type, void>)
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
requires std::same_as<typename Sender::value_type, void>
struct [[nodiscard]] transform_sender<Sender, F> {
	using value_type = std::invoke_result_t<F>;

	template<Receives<value_type> Receiver>
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
// ite()
//---------------------------------------------------------------------------------------

template<typename C, typename ST, typename SE, typename R>
struct [[nodiscard]] ite_operation {
	ite_operation(C cond, ST then_s, SE else_s, R dr)
	: cond_{std::move(cond)}, then_s_{std::move(then_s)}, else_s_{std::move(else_s)},
			dr_{std::move(dr)} { }

	ite_operation(const ite_operation &) = delete;

	ite_operation &operator= (const ite_operation &) = delete;

	~ite_operation() {
		if(then_op_.valid())
			then_op_.destruct();
		if(else_op_.valid())
			else_op_.destruct();
		}

	bool start_inline() {
		if(cond_()) {
			then_op_.construct_with([&] {
				return execution::connect(std::move(then_s_), std::move(dr_));
			});
			return execution::start_inline(*then_op_);
		}else{
			else_op_.construct_with([&] {
				return execution::connect(std::move(else_s_), std::move(dr_));
			});
			return execution::start_inline(*else_op_);
		}
	}

	C cond_;
	ST then_s_;
	SE else_s_;
	R dr_;
	frg::manual_box<execution::operation_t<ST, R>> then_op_;
	frg::manual_box<execution::operation_t<SE, R>> else_op_;
};

template<typename C, typename ST, typename SE>
struct [[nodiscard]] ite_sender {
	using value_type = typename ST::value_type;

	ite_sender(C cond, ST then_s, SE else_s)
	: cond_{std::move(cond)}, then_s_{std::move(then_s)}, else_s_{std::move(else_s)} { }

	template<Receives<value_type> R>
	ite_operation<C, ST, SE, R> connect(R dr) {
		return {std::move(cond_), std::move(then_s_), std::move(else_s_), std::move(dr)};
	}

	sender_awaiter<ite_sender, value_type> operator co_await () {
		return {std::move(*this)};
	}

private:
	C cond_;
	ST then_s_;
	SE else_s_;
};

template<std::invocable<> C, Sender ST, Sender SE>
requires std::same_as<typename ST::value_type, typename SE::value_type>
ite_sender<C, ST, SE> ite(C cond, ST then_s, SE else_s) {
	return {std::move(cond), std::move(then_s), std::move(else_s)};
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

	template<Receives<value_type> R>
	repeat_while_operation<C, SF, R> connect(R receiver) {
		return {std::move(cond), std::move(factory), std::move(receiver)};
	}

	C cond;
	SF factory;
};

template<typename C, typename SF>
requires std::move_constructible<C> && requires (C c, SF sf) {
	{ c() } -> std::convertible_to<bool>;
	{ sf() } -> Sender;
}
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

	template<Receives<value_type> Receiver>
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

		void set_value_inline() {
		}

		void set_value_noinline() {
			auto n = self_->n_done_.fetch_add(1, std::memory_order_acq_rel);
			if(!n) {
				for(unsigned int j = 0; j < sizeof...(Is); ++j)
					if(j != I)
						self_->cs_[j].cancel();
			}
			if(n + 1 == sizeof...(Is))
				execution::set_value_noinline(self_->r_);
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
	: r_{std::move(r)}, ops_{make_operations_tuple(std::move(s))}, n_sync_{0}, n_done_{0} { }

	bool start_inline() {
		unsigned int n_sync = 0;

		((execution::start_inline(ops_.template get<Is>())
			? n_sync++ : 0), ...);

		if (n_sync) {
			auto n = n_done_.fetch_add(n_sync, std::memory_order_acq_rel);

			if (!n) {
				for(unsigned int j = 0; j < sizeof...(Is); ++j)
					cs_[j].cancel();
			}

			if ((n + n_sync) == sizeof...(Is)) {
				execution::set_value_inline(r_);
				return true;
			}
		}

		return false;
	}

private:
	Receiver r_;
	operation_tuple ops_;
	cancellation_event cs_[sizeof...(Is)];
	std::atomic<unsigned int> n_sync_;
	std::atomic<unsigned int> n_done_;
};

template<typename... Functors>
sender_awaiter<race_and_cancel_sender<Functors...>>
operator co_await(race_and_cancel_sender<Functors...> s) {
	return {std::move(s)};
}

template<std::invocable<cancellation_token>... Functors>
requires ((Sender<std::invoke_result_t<Functors, cancellation_token>>) && ...)
race_and_cancel_sender<Functors...> race_and_cancel(Functors... fs) {
	return {{std::move(fs)...}};
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
	bool start_inline() {
		imm_ = std::move(pred_());
		op_.construct_with([&]{ return execution::connect(func_(imm_), std::move(r_)); });
		return execution::start_inline(*op_);
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

	template<Receives<value_type> Receiver>
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

template <std::invocable<> Pred, typename Func>
requires requires (Func func, Pred pred) {
	func(std::declval<std::add_lvalue_reference_t<decltype(pred())>>());
}
let_sender<Pred, Func> let(Pred pred, Func func) {
	return {std::move(pred), std::move(func)};
}

//---------------------------------------------------------------------------------------
// sequence()
//---------------------------------------------------------------------------------------

template <typename R, typename ...Senders> requires (sizeof...(Senders) > 0)
struct [[nodiscard]] sequence_operation {
	sequence_operation(frg::tuple<Senders...> senders, R dr)
	: senders_{std::move(senders)}, dr_{std::move(dr)} { }

	sequence_operation(const sequence_operation &) = delete;

	sequence_operation &operator=(const sequence_operation &) = delete;

	bool start_inline() {
		return do_step<0, true>();
	}

private:
	template <size_t Index>
	using nth_sender = std::tuple_element_t<Index, frg::tuple<Senders...>>;

	template <size_t Index, bool InlinePath>
	requires (InlinePath)
	bool do_step() {
		using operation_type = execution::operation_t<nth_sender<Index>,
				receiver<Index, true>>;

		auto op = new (box_.buffer) operation_type{
			execution::connect(std::move(senders_.template get<Index>()),
					receiver<Index, true>{this})
		};

		if(execution::start_inline(*op)) {
			if constexpr (Index == sizeof...(Senders) - 1) {
				return true;
			}else{
				return do_step<Index + 1, true>();
			}
		}
		return false;
	}

	// Same as above but since we are not on the InlinePath, we do not care about the return value.
	template <size_t Index, bool InlinePath>
	requires (!InlinePath)
	void do_step() {
		using operation_type = execution::operation_t<nth_sender<Index>,
				receiver<Index, false>>;

		auto op = new (box_.buffer) operation_type{
			execution::connect(std::move(senders_.template get<Index>()),
					receiver<Index, false>{this})
		};

		if(execution::start_inline(*op)) {
			if constexpr (Index != sizeof...(Senders) - 1)
				do_step<Index + 1, false>();
		}
	}

	template <size_t Index, bool InlinePath>
	struct receiver {
		using value_type = typename nth_sender<Index>::value_type;
		static_assert((Index == sizeof...(Senders) - 1) || std::is_same_v<value_type, void>,
				"All but the last sender must return void");

		receiver(sequence_operation *self)
		: self_{self} { }

		void set_value_inline() requires (Index < sizeof...(Senders) - 1) {
			using operation_type = execution::operation_t<nth_sender<Index>,
					receiver<Index, InlinePath>>;
			auto op = std::launder(reinterpret_cast<operation_type *>(self_->box_.buffer));
			op->~operation_type();

			// Do nothing: execution continues in do_step().
		}

		void set_value_noinline() requires (Index < sizeof...(Senders) - 1) {
			using operation_type = execution::operation_t<nth_sender<Index>,
					receiver<Index, InlinePath>>;
			auto s = self_; // following lines will destruct this.
			auto op = std::launder(reinterpret_cast<operation_type *>(s->box_.buffer));
			op->~operation_type();

			// Leave the inline path.
			s->template do_step<Index + 1, false>();
		}

		void set_value_inline()
				requires ((Index == sizeof...(Senders) - 1)
						&& (std::is_same_v<value_type, void>)) {
			using operation_type = execution::operation_t<nth_sender<Index>,
					receiver<Index, InlinePath>>;
			auto s = self_; // following lines will destruct this.
			auto op = std::launder(reinterpret_cast<operation_type *>(s->box_.buffer));
			op->~operation_type();

			if(InlinePath) {
				execution::set_value_inline(s->dr_);
			}else{
				execution::set_value_noinline(s->dr_);
			}
		}

		void set_value_noinline()
				requires ((Index == sizeof...(Senders) - 1)
						&& (std::is_same_v<value_type, void>)) {
			using operation_type = execution::operation_t<nth_sender<Index>,
					receiver<Index, InlinePath>>;
			auto s = self_; // following lines will destruct this.
			auto op = std::launder(reinterpret_cast<operation_type *>(s->box_.buffer));
			op->~operation_type();

			execution::set_value_noinline(s->dr_);
		}

		template <typename T>
		void set_value_inline(T value)
				requires ((Index == sizeof...(Senders) - 1)
						&& (!std::is_same_v<value_type, void>)
						&& (std::is_same_v<value_type, T>)) {
			using operation_type = execution::operation_t<nth_sender<Index>,
					receiver<Index, InlinePath>>;
			auto s = self_; // following lines will destruct this.
			auto op = std::launder(reinterpret_cast<operation_type *>(s->box_.buffer));
			op->~operation_type();

			if(InlinePath) {
				execution::set_value_inline(s->dr_, std::move(value));
			}else{
				execution::set_value_noinline(s->dr_, std::move(value));
			}
		}

		template <typename T>
		void set_value_noinline(T value)
				requires ((Index == sizeof...(Senders) - 1)
						&& (!std::is_same_v<value_type, void>)
						&& (std::is_same_v<value_type, T>)) {
			using operation_type = execution::operation_t<nth_sender<Index>,
					receiver<Index, InlinePath>>;
			auto s = self_; // following lines will destruct this.
			auto op = std::launder(reinterpret_cast<operation_type *>(s->box_.buffer));
			op->~operation_type();

			execution::set_value_noinline(s->dr_, std::move(value));
		}

	private:
		sequence_operation *self_;
	};

	frg::tuple<Senders...> senders_;
	R dr_; // Downstream receiver.

	static constexpr size_t max_operation_size = []<size_t ...I>(std::index_sequence<I...>) {
		return std::max({sizeof(execution::operation_t<nth_sender<I>, receiver<I, true>>)...,
				sizeof(execution::operation_t<nth_sender<I>, receiver<I, false>>)...});
	}(std::make_index_sequence<sizeof...(Senders)>{});

	static constexpr size_t max_operation_alignment = []<size_t ...I>(std::index_sequence<I...>) {
		return std::max({alignof(execution::operation_t<nth_sender<I>, receiver<I, true>>)...,
			alignof(execution::operation_t<nth_sender<I>, receiver<I, false>>)...});
	}(std::make_index_sequence<sizeof...(Senders)>{});

	frg::aligned_storage<max_operation_size, max_operation_alignment> box_;
};

template <Sender ...Senders> requires (sizeof...(Senders) > 0)
struct [[nodiscard]] sequence_sender {
	using value_type = typename std::tuple_element_t<sizeof...(Senders) - 1, frg::tuple<Senders...>>::value_type;

	template<typename Receiver>
	friend sequence_operation<Receiver, Senders...>
	connect(sequence_sender s, Receiver r) {
		return {std::move(s.senders), std::move(r)};
	}

	frg::tuple<Senders...> senders;
};

template <Sender ...Senders> requires (sizeof...(Senders) > 0)
sequence_sender<Senders...> sequence(Senders ...senders) {
	return {frg::tuple<Senders...>{std::move(senders)...}};
}

template <Sender ...Senders>
sender_awaiter<sequence_sender<Senders...>, typename sequence_sender<Senders...>::value_type>
operator co_await(sequence_sender<Senders...> s) {
	return {std::move(s)};
}

//---------------------------------------------------------------------------------------
// when_all()
//---------------------------------------------------------------------------------------

template<typename Receiver, typename... Senders>
struct when_all_operation {
private:
	struct receiver {
		receiver(when_all_operation *self)
		: self_{self} { }

		void set_value_inline() {
			// Simply do nothing.
		}

		void set_value_noinline() {
			auto c = self_->ctr_.fetch_sub(1, std::memory_order_acq_rel);
			assert(c > 0);
			if(c == 1)
				execution::set_value_noinline(self_->dr_);
		}

	private:
		when_all_operation *self_;
	};

	template<size_t... Is>
	auto make_operations_tuple(std::index_sequence<Is...>, frg::tuple<Senders...> senders) {
		return frg::make_tuple(
			make_connect_helper(
				std::move(senders.template get<Is>()),
				receiver{this}
			)...
		);
	}

public:
	when_all_operation(frg::tuple<Senders...> senders, Receiver dr)
	: dr_{std::move(dr)},
		ops_{make_operations_tuple(std::index_sequence_for<Senders...>{}, std::move(senders))},
		ctr_{sizeof...(Senders)} { }

	bool start_inline() {
		int n_fast = 0;
		[&]<size_t... Is> (std::index_sequence<Is...>) {
			([&] <size_t I> () {
				if(execution::start_inline(ops_.template get<I>()))
					++n_fast;
			}.template operator()<Is>(), ...);
		}(std::index_sequence_for<Senders...>{});

		auto c = ctr_.fetch_sub(n_fast, std::memory_order_acq_rel);
		assert(c > 0);
		if(c == n_fast) {
			execution::set_value_inline(dr_);
			return true;
		}
		return false;
	}

	Receiver dr_; // Downstream receiver.
	frg::tuple<execution::operation_t<Senders, receiver>...> ops_;
	std::atomic<int> ctr_;
};

template <typename ...Senders> requires (sizeof...(Senders) > 0)
struct [[nodiscard]] when_all_sender {
	using value_type = void;

	template<Receives<value_type> Receiver>
	friend when_all_operation<Receiver, Senders...>
	connect(when_all_sender s, Receiver r) {
		return {std::move(s.senders), std::move(r)};
	}

	frg::tuple<Senders...> senders;
};

template <Sender ...Senders>
requires (sizeof...(Senders) > 0)
when_all_sender<Senders...> when_all(Senders ...senders) {
	return {frg::tuple<Senders...>{std::move(senders)...}};
}

template <typename ...Senders>
sender_awaiter<when_all_sender<Senders...>>
operator co_await(when_all_sender<Senders...> s) {
	return {std::move(s)};
}

//---------------------------------------------------------------------------------------
// lambda()
//---------------------------------------------------------------------------------------

template <typename R, typename Fn, typename ...Args>
struct lambda_operation {
	lambda_operation(R receiver, Fn fn, std::tuple<Args...> args)
	: fn_{std::move(fn)}, op_{execution::connect(std::apply(fn_, args), std::move(receiver))} { }

	bool start_inline() {
		return execution::start_inline(op_);
	}

	Fn fn_;
	execution::operation_t<std::invoke_result_t<Fn, Args...>, R> op_;
};

template <typename Fn, typename ...Args>
struct [[nodiscard]] lambda_sender {
	using value_type = std::invoke_result_t<Fn, Args...>::value_type;

	Fn fn;
	std::tuple<Args...> args;

	template<typename Receiver>
	friend lambda_operation<Receiver, Fn, Args...>
	connect(lambda_sender s, Receiver r) {
		return {std::move(r), std::move(s.fn), std::move(s.args)};
	}
};

template <typename Fn, typename ...Args>
sender_awaiter<lambda_sender<Fn, Args...>, typename lambda_sender<Fn, Args...>::value_type>
operator co_await(lambda_sender<Fn, Args...> s) {
	return {std::move(s)};
}

template <typename Fn>
struct [[nodiscard]] lambda_callable {
	template <typename ...Args>
	auto operator()(Args &&...args) {
		return lambda_sender<Fn, Args...>{
			std::move(fn),
			std::forward_as_tuple(std::forward<Args>(args)...)
		};
	}

	Fn fn;
};

template <typename Fn>
auto lambda(Fn fn) {
	return lambda_callable<Fn>{std::move(fn)};
}

} // namespace async
