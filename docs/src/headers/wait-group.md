# wait\_group

```cpp
#include <async/wait-group.hpp>
```

`wait_group` is a synchronization primitive that waits for a counter (that can
be incremented) to reach zero. It conceptually maps to a group of related work
being done in parallel, and a few consumers waiting for that work to be done.
The amount of work is increased by calls to `add()` and decreased by calls to
`done()`.

This struct also implements
[BasicLockable](https://en.cppreference.com/w/cpp/named_req/BasicLockable) so
that it can be used with `std::unique_lock`.

## Prototype

```cpp
struct wait_group {
	void done(); // (1)
	void add(int n); // (2)

	sender wait(cancellation_token ct); // (3)
	sender wait(); // (4)

	void lock(); // (5)
	void unlock(); // (6)
};
```

1. "Finishes" a work (decrements the work count).
2. "Adds" more work (increments the work count by `n`).
3. Returns a sender for the wait operation. The operation waits for the counter
   to drop to zero.
4. Same as (3) but it cannot be cancelled.
5. Equivalent to `add(1)`.
5. Equivalent to `done()`.

### Arguments

 - `n` - amount of work to "add" to this work group
 - `ct` - the cancellation token to use to listen for cancellation.

### Return values

1. This method doesn't return any value.
2. This method doesn't return any value.
3. This method returns a sender of unspecified type. The sender completes with
either `true` to indicate success, or `false` to indicate that the wait was cancelled.
4. Same as (3) except the sender completes without a value.

## Examples

```cpp
async::wait_group wg { 3 };

([&wg] () -> async::detached {
	std::cout << "before wait" << std::endl;
	co_await wg.wait();
	std::cout << "after wait" << std::endl;
})();

auto done = [&wg] () {
	std::cout << "before done" << std::endl;
	wg.done();
	std::cout << "after done" << std::endl;
};

done();
done();
std::cout << "before add" << std::endl;
wg.add(2);
std::cout << "after add" << std::endl;
done();
done();
done();
```

Output:

```
before wait
before done
after done
before done
after done
before add
after add
before done
after done
before done
after done
before done
after wait
after done
```
