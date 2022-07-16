# queue

```cpp
#include <async/queue.hpp>
```

`queue` is a type which provides a queue on which you can asynchronously wait
for items to appear.

## Prototype

```cpp
template <typename T, typename Allocator>
struct queue {
	queue(Allocator allocator = {}); // (1)

	void put(T item); // (2)

	template <typename ...Ts>
	void emplace(Ts &&...ts); // (3)

	sender async_get(cancellation_token ct = {}); // (4)

	frg::optional<T> maybe_get() // (5)
};
```

1. Constructs a queue with the given allocator.
2. Inserts an item into the queue.
3. Emplaces an item into the queue.
4. Returns a sender for the get operation. The operation waits for an item to be
inserted and returns it.
5. Pops and returns the top item if it exists, or `frg::null_opt` otherwise.

### Requirements

`T` is moveable. `Allocator` is an allocator.

3. `T` is constructible with `Ts`.

### Arguments
 - `allocator` - the allocator to use.
 - `item` - the item to insert into the queue.
 - `ts` - the arguments to pass to the constructor of `T` when inserting it into the queue.
 - `ct` - the cancellation token to use.

### Return values

1. N/A
2. This method doesn't return any value.
3. Same as (2).
4. This method returns a sender of unspecified type. The sender returns a
`frg::optional<T>` and completes with the value, or `frg::null_opt` if the
operation was cancelled.
5. This method returns a value of type `frg::optional<T>`. It returns a value
from the queue, or `frg::null_opt` if the queue is empty.

## Examples

```cpp
auto coro = [] (async::queue<int, frg::stl_allocator> &q) -> async::detached {
	std::cout << "Got " << *(co_await q.async_get()) << std::endl;
	std::cout << "Got " << *(co_await q.async_get()) << std::endl;
};

async::queue<int, frg::stl_allocator> q;

coro(q);

q.put(1);
q.put(2);
```

Output:
```
1
2
```
