#pragma once

#ifndef LIBASYNC_CUSTOM_PLATFORM
#include <mutex>
#include <iostream>
#include <cassert>
#include <optional>

namespace async::platform {
	using mutex = std::mutex;

	[[noreturn]] inline void panic(const char *str) {
		std::cerr << str << std::endl;
		std::terminate();
	}
} // namespace async::platform
#else
#include <async/platform.hpp>
#endif
