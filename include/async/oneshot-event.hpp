#pragma once

#include <async/algorithm.hpp>
#include <async/wait-group.hpp>
#include <async/cancellation.hpp>
#include <frg/functional.hpp>
#include <frg/list.hpp>

namespace async {

struct oneshot_event {
	void raise() {
		wg_.done();
	}

	auto wait(cancellation_token ct) {
		return wg_.wait(ct);
	}

	auto wait() {
		return wg_.wait();
	}
private:
	wait_group wg_ { 1 };
};

} // namespace async
