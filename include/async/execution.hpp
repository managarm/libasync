#pragma once

#include <type_traits>

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
        auto operator() (Sender s, Receiver r) const {
            if constexpr (has_connect_member_v<Sender, Receiver>)
				return s.connect(r);
            else if constexpr (has_global_connect_v<Sender, Receiver>)
				return connect(std::move(s), std::move(r));
			else
				static_assert(dependent_false_t<Sender, Receiver>,
						"No connect() customization defined for sender type");
        }
    };

	template<typename Op>
	using start_member_t = decltype(start(std::declval<Op>()));

	template<typename Op>
	constexpr bool has_start_member_v = is_detected_v<start_member_t, Op>;

	template<typename Op>
	using global_start_t = decltype(std::declval<Op>().start());

	template<typename Op>
	constexpr bool has_global_start_v = is_detected_v<global_start_t, Op>;

	struct start_cpo {
        template<typename Operation>
        auto operator() (Operation op) const {
            if constexpr (has_start_member_v<Operation>)
				return op.start();
            else if constexpr (has_global_start_v<Operation>)
				return start(std::move(op));
			else
				static_assert(dependent_false_t<Operation>,
						"No start() customization defined for operation type");
        }
    };
}

namespace execution {
    template<typename S, typename R>
    using operation_t = std::invoke_result_t<cpo_types::connect_cpo, S, R>;

	inline cpo_types::connect_cpo connect;
	inline cpo_types::start_cpo start;
}

} // namespace async
