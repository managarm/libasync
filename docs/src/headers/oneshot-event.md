---
short-description: Event type that can only be raised once
...

# oneshot_event

```cpp
#include <async/oneshot-event.hpp>
```

`oneshot_event` is an event type that can be only raised once, and supports
multiple waiters. After the initial raise, any further waits will complete
immediately, and further raises will be a no-op.

## Prototype

```cpp
struct oneshot_event {
	void raise(); // (1)

	sender wait(cancellation_token ct); // (2)
	sender wait(); // (3)
};
```

1. Raises an event.
2. Returns a sender for the wait operation. The operation waits for the event
to be raised.
3. Same as (2) but it cannot be cancelled.

### Arguments

 - `ct` - the cancellation token to use to listen for cancellation.

### Return values

1. This method doesn't return any value.
2. This method returns a sender of unspecified type. The sender completes with
either `true` to indicate success, or `false` to indicate that the wait was cancelled.
3. Same as (2) except the sender completes without a value.

## Examples

```cpp
async::oneshot_event ev;

auto coro = [] (async::oneshot_event &ev) -> async::detached {
	std::cout << "Before wait" << std::endl;
	co_await ev.wait();
	std::cout << "After wait" << std::endl;
};

coro(ev);
std::cout << "Before raise" << std::endl;
ev.raise();
std::cout << "After raise" << std::endl;
coro(ev);
```

Output:
```
Before wait
Before raise
After wait
After raise
Before wait
After wait
```
