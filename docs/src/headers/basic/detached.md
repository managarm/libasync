---
short-description: Functions and classes for detached coroutines
...

# detached, detach and detach_with_allocator

`detached` is a coroutine type used for detached coroutines. Detached coroutines
cannot be awaited and they do not suspend the coroutine that started them.

`detach` and `detach_with_allocator` are functions that take a sender and run them
as if they were a detached coroutine. `detach` is a wrapper around `detach_with_allocator`
that uses `operator new`/`operator delete` for allocating memory. The allocator
is used to allocate a structure that holds the operation and continuation until
the operation completes.

## Prototype

```cpp
template<typename Allocator, typename S, typename Cont>
void detach_with_allocator(Allocator allocator, S sender, Cont continuation); // (1)

template<typename Allocator, typename S>
void detach_with_allocator(Allocator allocator, S sender); // (2)

template<typename S>
void detach(S sender); // (3)

template<typename S, typename Cont>
void detach(S sender, Cont continuation); // (4)
```

1. Detach a sender using an allocator, and call the continuation after it completes.
2. Same as (1) but without the continuation.
3. Same as (2) but without an allocator.
4. Same as (1) but without an allocator.

### Requirements

`Allocator` is an allocator, `S` is a sender, `Cont` is a functor that takes no arguments.

### Arguments

 - `allocator` - the allocator to use.
 - `sender` - the sender to start.
 - `continuation` - the functor to call on completion.

### Return value

These functions don't return any value.

## Examples

```cpp
async::oneshot_event ev;

async::run([] (async::oneshot_event &ev) -> async::detached {
		std::cout << "Coroutine 1" << std::endl;
		co_await ev.wait();
		std::cout << "Coroutine 2" << std::endl;
	}(ev));

std::cout << "Before event raise" << std::endl;
ev.raise();
std::cout << "After event raise" << std::endl;
```

Output:
```
Coroutine 1
Before event raise
Coroutine 2
After event raise
```
