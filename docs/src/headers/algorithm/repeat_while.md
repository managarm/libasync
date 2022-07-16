# repeat_while

`repeat_while` is an operation that continuously checks the given condition, and
as long as it's true, it invokes the given functor to obtain a sender, and starts it.

## Prototype

```cpp
template <typename C, typename SF>
sender repeat_while(C cond, SF factory);
```

### Requirements

`C` is a functor that returns a truthy or falsy value. `SF` is a functor that
returns a sender.

### Arguments

 - `cond` - the condition to check.
 - `factory` - the functor to call to obtain the sender on every iteration.

### Return value

This function returns a sender of unspecified type. The sender does not return
any value.

## Examples

```cpp
int i = 0;
async::run(async::repeat_while([&] { return i++ < 5; },
		[] () -> async::result<void> {
			std::cout << "Hi" << std::endl;
			co_return;
		}));
```

Output:
```
Hi
Hi
Hi
Hi
Hi
```
