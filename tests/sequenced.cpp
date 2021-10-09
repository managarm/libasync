#include <async/sequenced-event.hpp>
#include <async/result.hpp>
#include <gtest/gtest.h>

TEST(SequencedEvent, WaitOne) {
	async::sequenced_event ev;
	ev.raise();
	auto seq = async::run(ev.async_wait(0));
	ASSERT_EQ(seq, 1);
}

TEST(SequencedEvent, WaitMultiple) {
	async::sequenced_event ev;
	ev.raise();
	auto seq1 = async::run(ev.async_wait(0));
	ev.raise();
	ev.raise();
	auto seq2 = async::run(ev.async_wait(seq1));
	ASSERT_EQ(seq1, 1);
	ASSERT_EQ(seq2, 3);
}

TEST(SequencedEvent, WaitCancel) {
	async::cancellation_event ce;
	async::sequenced_event ev;
	ce.cancel();
	auto seq = async::run(ev.async_wait(0, ce));
	ASSERT_EQ(seq, 0);
}
