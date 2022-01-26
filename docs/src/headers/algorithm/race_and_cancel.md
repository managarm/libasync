---
short-description: The race_and_cancel algorithm
...

# race_and_cancel

`race_and_cancel` is an operation that obtains senders using the given functors,
starts all of them concurrently, and cancels the remaining ones when one finishes.

**Note:** `race_and_cancel` does not guarantee that only one sender completes
without cancellation.

## Prototype

```cpp
template <typename... Functors>
sender race_and_cancel(Functors... fs);
```

### Requirements

Every type of `Functors` is invocable with an argument of type `async::cancellation_token`
and produces a sender.

### Arguments

 - `fs` - the functors to invoke to obtain the senders.

### Return value

This function returns a sender of unspecified type. The sender does not return any value.

## Examples

```cpp
async::run(async::race_and_cancel(
	[] (async::cancellation_token) -> async::result<void> {
		std::cout << "Hi 1" << std::endl;
		co_return;
	},
	[] (async::cancellation_token) -> async::result<void> {
		std::cout << "Hi 2" << std::endl;
		co_return;
	}
));
```

Possible output:
```
Hi 1
Hi 2
```
