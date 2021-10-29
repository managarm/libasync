#pragma once

#include <type_traits>
#include <utility>

namespace async {

// Detection pattern boilerplate.

template<typename... Ts>
using void_t = void;

template<typename... Ts>
constexpr bool dependent_false_t = false;

template <template <typename...> typename Trait, typename Void, typename... Args>
struct is_detected_helper : std::false_type { };

template <template <typename...> typename Trait, typename... Args>
struct is_detected_helper<Trait, void_t<Trait<Args...>>, Args...> : std::true_type { };

template <template <typename...> typename Trait, typename... Args>
constexpr bool is_detected_v = is_detected_helper<Trait, void, Args...>::value;

namespace cpo_types {
	// TODO: Rewrite this using concepts.
	template<typename S, typename R>
	using connect_member_t = decltype(std::declval<S>().connect(std::declval<R>()));

	template<typename S, typename R>
	constexpr bool has_connect_member_v = is_detected_v<connect_member_t, S, R>;

	template<typename S, typename R>
	using global_connect_t = decltype(connect(std::declval<S>(), std::declval<R>()));

	template<typename S, typename R>
	constexpr bool has_global_connect_v = is_detected_v<global_connect_t, S, R>;

	struct connect_cpo {
		template<typename Sender, typename Receiver>
		auto operator() (Sender &&s, Receiver &&r) const {
			if constexpr (has_connect_member_v<Sender, Receiver>)
					return s.connect(r);
			else if constexpr (has_global_connect_v<Sender, Receiver>)
					return connect(std::forward<Sender>(s), std::forward<Receiver>(r));
				else
					static_assert(dependent_false_t<Sender, Receiver>,
							"No connect() customization defined for sender type");
		}
	};

	template<typename Op>
	using start_member_t = decltype(std::declval<Op>().start());

	template<typename Op>
	constexpr bool has_start_member_v = is_detected_v<start_member_t, Op>;

	template<typename Op>
	using global_start_t = decltype(start(std::declval<Op>()));

	template<typename Op>
	constexpr bool has_global_start_v = is_detected_v<global_start_t, Op>;

	struct start_inline_cpo {
		template<typename Operation>
		bool operator() (Operation &&op) const {
			if constexpr (requires { op.start_inline(); }) {
				return op.start_inline();
			}else if constexpr (has_start_member_v<Operation>) {
				op.start();
				return false;
			}else if constexpr (has_global_start_v<Operation>) {
				start(std::forward<Operation>(op));
				return false;
			}else{
				static_assert(dependent_false_t<Operation>,
						"No start() customization defined for operation type");
			}
		}
	};

	struct set_value_cpo {
		template<typename Receiver, typename T>
		requires
			requires(Receiver &&r, T &&value) { r.set_value_noinline(std::forward<T>(value)); }
		void operator() (Receiver &&r, T &&value) {
			if constexpr (requires { r.set_value_noinline(std::forward<T>(value)); })
				r.set_value_noinline(std::forward<T>(value));
			else
				// This should have been caught by the concept.
				static_assert(dependent_false_t<Receiver>);
		}

		template<typename Receiver>
		requires
			requires(Receiver &&r) { r.set_value_noinline(); }
		void operator() (Receiver &&r) {
			if constexpr (requires { r.set_value_noinline(); })
				r.set_value_noinline();
			else
				// This should have been caught by the concept.
				static_assert(dependent_false_t<Receiver>);
		}
	};

	struct set_value_inline_cpo {
		template<typename Receiver, typename T>
		requires
			requires(Receiver &&r, T &&value) { r.set_value_inline(std::forward<T>(value)); }
		void operator() (Receiver &&r, T &&value) {
			if constexpr (requires { r.set_value_inline(std::forward<T>(value)); })
				r.set_value_inline(std::forward<T>(value));
			else
				// This should have been caught by the concept.
				static_assert(dependent_false_t<Receiver>);
		}

		template<typename Receiver>
		requires
			requires(Receiver &&r) { r.set_value_inline(); }
		void operator() (Receiver &&r) {
			if constexpr (requires { r.set_value_inline(); })
				r.set_value_inline();
			else
				// This should have been caught by the concept.
				static_assert(dependent_false_t<Receiver>);
		}
	};

	struct set_value_noinline_cpo {
		template<typename Receiver, typename T>
		requires
			requires(Receiver &&r, T &&value) { r.set_value_noinline(std::forward<T>(value)); }
		void operator() (Receiver &&r, T &&value) {
			if constexpr (requires { r.set_value_noinline(std::forward<T>(value)); })
				r.set_value_noinline(std::forward<T>(value));
			else
				// This should have been caught by the concept.
				static_assert(dependent_false_t<Receiver>);
		}

		template<typename Receiver>
		requires
			requires(Receiver &&r) { r.set_value_noinline(); }
		void operator() (Receiver &&r) {
			if constexpr (requires { r.set_value_noinline(); })
				r.set_value_noinline();
			else
				// This should have been caught by the concept.
				static_assert(dependent_false_t<Receiver>);
		}
	};
}

namespace execution {
	template<typename S, typename R>
	using operation_t = std::invoke_result_t<cpo_types::connect_cpo, S, R>;

	inline cpo_types::connect_cpo connect;
	inline cpo_types::start_inline_cpo start_inline;
	inline cpo_types::set_value_cpo set_value;
	inline cpo_types::set_value_inline_cpo set_value_inline;
	inline cpo_types::set_value_noinline_cpo set_value_noinline;
}

} // namespace async
