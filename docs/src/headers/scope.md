# async/scope.hpp

```cpp
#include <async/scope.hpp>
```

`dynamic_scope` and `dynamic_deferred_scope` are operations that run a number of
senders concurrently, where the number is only known at runtime. The senders are
supplied by a fill function, and the operation only completes once the fill
function has returned and all of the senders have completed.

## Prototype

```cpp
template<typename Sender, typename Allocator, typename Fill>
sender dynamic_scope(Allocator allocator, Fill fill); // (1)

template<typename Sender, typename Allocator, typename Fill>
sender dynamic_deferred_scope(Allocator allocator, Fill fill); // (2)
```

1. Calls `fill(spawn)`. Each call to `spawn(sender)` starts `sender`
immediately.
2. Calls `fill(spawn)`. The senders passed to `spawn()` are only started after
`fill` returns.

### Requirements

`Sender` is a sender that doesn't return any value. `Fill` is invocable with a
reference to a callable that takes a `Sender`.

`spawn()` may only be called from within `fill`, on the thread that runs
`fill`. It must not be called after `fill` returns and it must not be called
concurrently.

### Arguments

 - `allocator` - the allocator used to allocate the state of each sender.
 - `fill` - the function that supplies the senders.

### Return value

This function returns a sender of unspecified type. The sender does not return
any value.

The senders passed to `spawn()` are started with the environment of the
receiver that the returned sender is connected to.

With (1), a sender may already have completed by the time that `spawn()`
returns. (2) guarantees that no sender runs before `fill` returns. This is
useful if `fill` runs in a context that the senders must not run in (e.g., while
a lock is held).

## Examples

```cpp
async::run(async::dynamic_deferred_scope<async::result<void>>(frg::stl_allocator{},
	[] (auto &spawn) {
		for(int i = 0; i < 3; i++) {
			spawn([] (int i) -> async::result<void> {
				std::cout << "Hi " << i << std::endl;
				co_return;
			}(i));
			std::cout << "Spawned " << i << std::endl;
		}
	}
));
std::cout << "Done" << std::endl;
```

Output:
```
Spawned 0
Spawned 1
Spawned 2
Hi 0
Hi 1
Hi 2
Done
```
