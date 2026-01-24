#pragma once

#include <type_traits>
#include <utility>
#include <concepts>

#include <frg/detection.hpp>

namespace async {
namespace cpo_types {

template<typename Sender, typename Receiver>
concept member_connect =  requires (Sender &&s, Receiver &&r) {
	std::forward<Sender>(s).connect(std::forward<Receiver>(r));
};

template<typename Sender, typename Receiver>
concept global_connect = requires (Sender &&s, Receiver &&r) {
	connect(std::forward<Sender>(s), std::forward<Receiver>(r));
};

struct connect_cpo {
	template<typename Sender, typename Receiver>
	auto operator() (Sender &&s, Receiver &&r) const {
		if constexpr (member_connect<Sender, Receiver>) {
			return std::forward<Sender>(s).connect(std::forward<Receiver>(r));
		} else if constexpr (global_connect<Sender, Receiver>) {
			return connect(
				std::forward<Sender>(s),
				std::forward<Receiver>(r)
			);
		} else {
			static_assert(frg::dependent_false_t<Sender, Receiver>,
				"No connect() customization defined for S,R");
		}
	}
};

template<typename Operation>
concept member_start = requires (Operation &&op) {
	std::forward<Operation>(op).start();
};

template<typename Operation>
concept global_start = requires (Operation &&op) {
	start(std::forward<Operation>(op));
};

struct start_cpo {
	template<typename Operation>
	void operator() (Operation &&op) const {
		if constexpr (member_start<Operation>) {
			std::forward<Operation>(op).start();
		}else if constexpr (global_start<Operation>) {
			start(std::forward<Operation>(op));
		}else{
			static_assert(frg::dependent_false_t<Operation>,
				"No start() customization defined for operation type");
		}
	}
};

struct start_inline_cpo {
	template<typename Operation>
	bool operator() (Operation &&op) const {
		if constexpr (member_start<Operation>) {
			std::forward<Operation>(op).start();
			return false;
		}else if constexpr (global_start<Operation>) {
			start(std::forward<Operation>(op));
			return false;
		}else{
			static_assert(frg::dependent_false_t<Operation>,
				"No start() customization defined for operation type");
		}
	}
};

struct set_value_cpo {
	template<typename Receiver, typename T>
	requires requires(Receiver &&r, T &&value) {
		std::forward<Receiver>(r).set_value(std::forward<T>(value));
	}
	void operator() (Receiver &&r, T &&value) {
		std::forward<Receiver>(r).set_value(std::forward<T>(value));
	}

	template<typename Receiver>
	requires requires(Receiver &&r) {
		std::forward<Receiver>(r).set_value();
	}
	void operator() (Receiver &&r) {
		std::forward<Receiver>(r).set_value();
	}

	// Obsolete set_value_noinline() functions.
	template<typename Receiver, typename T>
	requires requires(Receiver &&r, T &&value) {
		std::forward<Receiver>(r).set_value_noinline(std::forward<T>(value));
	}
	void operator() (Receiver &&r, T &&value) {
		std::forward<Receiver>(r).set_value_noinline(std::forward<T>(value));
	}

	template<typename Receiver>
	requires requires(Receiver &&r) {
		std::forward<Receiver>(r).set_value_noinline();
	}
	void operator() (Receiver &&r) {
		std::forward<Receiver>(r).set_value_noinline();
	}
};

} // namespace cpo_types

namespace execution {
	template<typename S, typename R>
	using operation_t = std::invoke_result_t<cpo_types::connect_cpo, S, R>;

	inline cpo_types::connect_cpo connect;
	inline cpo_types::start_cpo start;
	inline cpo_types::start_inline_cpo start_inline;
	inline cpo_types::set_value_cpo set_value;
}

} // namespace async
