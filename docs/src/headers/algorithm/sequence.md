# sequence

`sequence` is an operation that sequentially starts the given senders.

## Prototype

```cpp
template <typename ...Senders>
sender sequence(Senders ...senders);
```

### Requirements

`Senders` is not empty, and every type in it is a sender. All but the last sender
must have a return type of `void`.

### Arguments

 - `senders` - the senders to run.

### Return value

This function returns a sender of unspecified type. The sender returns the value
returned by the last sender.

## Examples

```cpp
int steps[4] = {0, 0, 0, 0};
int v = async::run([&]() -> async::result<int> {
	int i = 0;
	co_return co_await async::sequence(
		[&]() -> async::result<void> {
			steps[0] = i;
			i++;
			co_return;
		}(),
		[&]() -> async::result<void> {
			steps[1] = i;
			i++;
			co_return;
		}(),
		[&]() -> async::result<void> {
			steps[2] = i;
			i++;
			co_return;
		}(),
		[&]() -> async::result<int> {
			steps[3] = i;
			i++;
			co_return i * 10;
		}()
	);
}());

std::cout << v << " " << steps[0] << steps[1] << steps[2] << steps[3] << std::endl;
```

Output:
```
40 0123
```
