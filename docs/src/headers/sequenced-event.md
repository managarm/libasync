---
short-description: >-
  Event type that maintains a sequence number to detect missed
  raises and can be raised multiple times
...

# sequenced_event

```cpp
#include <async/sequenced-event.hpp>
```

`sequenced_event` is an event type that can be raised multiple times, and supports
multiple waiters. On raise, all waiters are woken up sequentially. The event
maintains a sequence counter to detect missed raises.

## Prototype

```cpp
struct sequenced_event {
	void raise(); // (1)

	uint64_t next_sequence(); // (2)

	sender async_wait(uint64_t in_seq, async::cancellation_token ct = {}); // (3)
};
```

1. Raises an event.
2. Returns the next sequence number for the event (the sequence number after the
event is raised).
3. Returns a sender for the wait operation. The operation checks whether the input
sequence is equal to the event sequeence, and if it's true, waits for the event to be raised.

### Arguments

 - `in_seq` - input sequence number to compare against the event sequence number.
 - `ct` - the cancellation token to use to listen for cancellation.

### Return values

1. This method doesn't return any value.
2. This method returns the next sequence number for the event (the sequence number
after the event is raised).
3. This method returns a sender of unspecified type. The sender completes with the
current event sequence number.

## Examples

```cpp
async::sequenced_event ev;

std::cout << ev.next_sequence() << std::endl;
ev.raise();
std::cout << ev.next_sequence() << std::endl;

auto seq = async::run(
		[] (async::sequenced_event &ev) -> async::result<uint64_t> {
			// Current sequence is 1, so waiting at sequence 0 will immediately complete.
			co_return co_await ev.async_wait(0);
		}(ev));

std::cout << seq << std::endl;
```

Output:
```
1
2
1
```
