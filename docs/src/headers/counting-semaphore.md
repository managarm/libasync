# counting-semaphore

```cpp
#include <async/counting-semaphore.hpp>
```

`counting_semaphore` is a semaphore holding a number of units on which you can
asynchronously wait until enough units become available.

It is *fair*: waiters are served in strict FIFO order and never overtake each
other. It is also *all-or-nothing*: a waiter acquires all of the units that it
requested at once, or none of them. Together this means that a waiter blocks
whenever an earlier waiter is still queued, even if enough units are available
to satisfy it right away.

## Prototype

```cpp
struct counting_semaphore {
	counting_semaphore(size_t count = 0); // (1)

	sender async_acquire(size_t n = 1); // (2)

	bool try_acquire(size_t n = 1); // (3)

	void release(size_t n = 1); // (4)
};
```

1. Constructs a semaphore holding `count` units.
2. Asynchronously acquires `n` units.
3. Synchronously tries to acquire `n` units.
4. Returns `n` units to the semaphore.

### Arguments
 - `count` - the number of units that the semaphore initially holds.
 - `n` - the number of units to acquire or release.

### Return values

1. N/A
2. This method returns a sender of unspecified type. The sender does not return
any value, and completes once all `n` units are acquired.
3. This method returns `true` if the units were successfully acquired, `false`
otherwise. Note that it also returns `false` if units are available but another
waiter is already queued, since that waiter must not be overtaken.
4. This method doesn't return any value.

The semaphore has no fixed capacity. `count` is only the number of units that
it starts out with, and `release()` does not have to be paired with a preceding
acquisition; both simply add units to the semaphore.

Consequently, `n` is not bounded by `count` either. Acquiring more units than
the semaphore currently holds is not an error: the operation waits until enough
units have been released. Note, however, that a request which is never
satisfied blocks every later waiter indefinitely, since waiters are never
overtaken.

## Examples

```cpp
async::counting_semaphore sem{0};

auto coro = [] (int i, size_t n, auto &sem) -> async::detached {
	co_await sem.async_acquire(n);
	std::cout << i << ": acquired " << n << std::endl;
};

// The first waiter needs more units than the second one.
coro(1, 3, sem);
coro(2, 1, sem);

// Not enough for the first waiter, and the second one must not overtake it.
sem.release(2);
std::cout << "released 2" << std::endl;
sem.release(2);
```

Output:
```
released 2
1: acquired 3
2: acquired 1
```
