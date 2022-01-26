---
short-description: Operation which suspends until it's cancelled
...

# suspend_indefinitely

`suspend_indefinitely` is an operation that suspends until it is cancelled.

## Prototype

```cpp
sender suspend_indefinitely(cancellation_token cancellation);
```

### Arguments

 - `cancellation` - the cancellation token to use.

### Return value

This function returns a sender of unspecified type. The sender doesn't return any
value, and completes when cancellation is requested.

## Examples

```cpp
async::cancellation_event ce;

auto coro = [] (async::cancellation_token ct) -> async::detached {
	std::cout << "Before await" << std::endl;
	co_await async::suspend_indefinitely(ct);
	std::cout << "After await" << std::endl;
};

coro(ce);
std::cout << "Before cancel" << std::endl;
ce.cancel();
std::cout << "After cancel" << std::endl;
```

Output:
```
Before await
Before cancel
After await
After cancel
```
