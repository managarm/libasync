#pragma once

#include <async/basic.hpp>
#include <optional>
#include <utility>

namespace async {

template <typename T>
struct generator {
	struct promise_type;
	using handle_type = corons::coroutine_handle<promise_type>;

	struct continuation {
		virtual void complete() = 0;
	};

	struct promise_type {
		std::optional<T> current_value;
		continuation *cont = nullptr;

		generator get_return_object() {
			return generator{corons::coroutine_handle<promise_type>::from_promise(*this)};
		}

		corons::suspend_always initial_suspend() noexcept { return {}; }

		struct yield_awaiter {
			bool await_ready() noexcept { return false; }

			void await_suspend(handle_type h) noexcept {
				auto cont = h.promise().cont;
				h.promise().cont = nullptr;
				if (cont) {
					cont->complete();
					return;
				}
				FRG_INTF(panic)("Generator yielded but no consumer is waiting");
			}

			void await_resume() noexcept {}
		};

		yield_awaiter yield_value(T value) noexcept {
			current_value = std::move(value);
			return yield_awaiter{};
		}

		yield_awaiter final_suspend() noexcept {
			return yield_awaiter{};
		}

		void return_void() {}

		void unhandled_exception() {
			FRG_INTF(panic)("Unhandled exception in generator coroutine");
		}
	};

	explicit generator(corons::coroutine_handle<promise_type> h) : h_(h) {}

	generator(generator &&other) : h_(std::exchange(other.h_, nullptr)) {}

	generator &operator=(generator &&other) {
		auto h = std::exchange(other.h_, nullptr);
		if (h_)
			h_.destroy();
		h_ = h;
		return *this;
	}

	~generator() {
		if (h_)
			h_.destroy();
	}

	template <typename Receiver>
	struct next_operation : continuation {
		next_operation(handle_type h, Receiver r)
		: h_{h}, r_{std::move(r)} {}

		void start() {
			h_.promise().cont = this;
			h_.resume();
		}

		void complete() override {
			auto val = std::move(h_.promise().current_value);
			h_.promise().current_value = {};
			execution::set_value(std::move(r_), std::move(val));
		}

		handle_type h_;
		Receiver r_;
	};

	struct next_sender {
		handle_type h_;
		using value_type = std::optional<T>;

		friend sender_awaiter<next_sender, next_sender::value_type> operator co_await (next_sender s) {
			return {s};
		}

		template <typename Receiver>
		next_operation<Receiver> connect(Receiver r) {
			return {h_, std::move(r)};
		}
	};

	next_sender next() {
		return {h_};
	}

private:
	corons::coroutine_handle<promise_type> h_;
};

} // namespace async
