---
short-description: The when_all algorithm
...

# when_all

`when_all` is an operation that starts all the given senders concurrently, and
only completes when all of them complete.

## Prototype

```cpp
template <typename... Senders>
sender when_all(Senders... senders);
```

### Requirements

Every type of `Senders` is a sender that doesn't return any value.

### Arguments

 - `senders` - the senders to start.

### Return value

This function returns a sender of unspecified type. The sender does not return
any value.

## Examples

```cpp
async::run(async::when_all(
	[] () -> async::result<void> {
		std::cout << "Hi 1" << std::endl;
		co_return;
	}(),
	[] () -> async::result<void> {
		std::cout << "Hi 2" << std::endl;
		co_return;
	}()
));
std::cout << "Done" << std::endl;
```

Possible output:
```
Hi 1
Hi 2
Done
```
