# lambda

`lambda` is a helper function which wraps a function-like object to
extend it's lifetime for the duration of the async operation. Primarily
intended to be used with lambdas with captures as arguments to
`race_and_cancel` and such.

The returned wrapper is callable with the same arguments as the
original function object, and the arguments get forwarded to it.

## Prototype

```cpp
template <typename Fn>
lambda_callable lambda(Fn fn);
```

### Requirements

`Fn` is a function-like object that produces a sender.

### Arguments

 - `fn` - the function-like object to wrap.

### Return value

This function returns a wrapper object that upon being called returns
a sender of unspecified type. The sender forwards the return value
from the sender returned by `fn`.

## Examples

```cpp
async::run(
	async::race_and_cancel(
		async::lambda(
			[x_ = std::make_shared<int>(1)] (async::cancellation_token ct)
			-> async::result<void> {
				// x_ is valid here
				std::cout << *x_ << "\n";
				co_return;
				/* ... */
			}
		),
		[x_ = std::make_shared<int>(2)] (async::cancellation_token ct)
		-> async::result<void> {
			// x_ is NOT valid here
			// the hidden lambda `this` is out of scope!
			// std::cout << *x_ << "\n"; // crash !
			co_return;
		}
	)
);
```

Output:
```
1
```
