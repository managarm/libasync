# recurring_event

```cpp
#include <async/recurring-event.hpp>
```

`recurring_event` is an event type that can be raised multiple times, and supports
multiple waiters. On raise, all waiters are woken up sequentially.

## Prototype

```cpp
struct recurring_event {
	void raise(); // (1)

	template <typename C>
	sender async_wait_if(C cond, cancellation_token ct = {}); // (2)
	sender async_wait(cancellation_token ct = {}); // (3)
};
```

1. Raises an event.
2. Returns a sender for the wait operation. The operation checks the condition, and
if it's true, waits for the event to be raised.
3. Same as (2) but without the condition.

### Requirements

`C` is a functor that accepts no arguments and returns a truthy or falsy value.

### Arguments

- `ct` - the cancellation token to use to listen for cancellation.

### Return values

1. This method doesn't return any value.
2. This method returns a sender of unspecified type. The value_type is of type `wait_if_result`,
which is `success` to indicate success, or `cancelled` to indicate that the wait was cancelled,
or `conditionFailed` when the condition was false.
3. This method returns `true` on success and `false` on cancellation.

## Examples

```cpp
async::recurring_event ev;

auto coro = [] (int i, async::recurring_event &ev) -> async::detached {
	std::cout << i << ": Before wait" << std::endl;
	co_await ev.async_wait();
	std::cout << i << ": After wait" << std::endl;
};

coro(1, ev);
coro(2, ev);
std::cout << "Before raise" << std::endl;
ev.raise();
std::cout << "After raise" << std::endl;
coro(3, ev);
```

Possible output:

```
1: Before wait
2: Before wait
Before raise
1: After wait
2: After wait
After raise
3: Before wait
```
