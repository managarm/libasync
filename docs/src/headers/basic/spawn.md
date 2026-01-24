# spawn_with_allocator

`spawn_with_allocator` is a function that takes a sender and a receiver, connects
them and detaches in a way similar to [`detach_with_allocator`](headers/basic/detached.md).

## Prototype

```cpp
template<typename Allocator, typename S, typename R>
void spawn_with_allocator(Allocator allocator, S sender, R receiver);
```

### Requirements

`Allocator` is an allocator, `S` is a sender, `R` is a receiver.

### Arguments

 - `allocator` - the allocator to use.
 - `sender` - the sender to start.
 - `receiver` - the receiver to use.

### Return value

This function doesn't return any value.

## Examples

```cpp
struct my_receiver {
	void set_value(int value) {
		std::cout << "Value: " << value << std::endl;
	}
};

async::oneshot_event ev;

async::spawn_with_allocator(frg::stl_allocator{},
		[] (async::oneshot_event &ev) -> async::result<int> {
			std::cout << "Start sender" << std::endl;
			co_await ev.wait();
			co_return 1;
		}(ev), my_receiver{});

std::cout << "Before event raise" << std::endl;
ev.raise();
std::cout << "After event raise" << std::endl;
```

Output:
```
Start sender
Before event raise
Value: 1
After event raise
```
