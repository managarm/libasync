# Writing low-level data structures

Writing low-level data structures with correct cancellation support can be difficult
and prone to errors. The `cancellation_resolver` data structure can help to simplify this issue.

Low level data structures (such as wait queues) will typically have a `struct node` (or similar)
that is linked to the data structure during an async operation.

```cpp
struct node {
    // Called when the async operation completes successfully (no cancellation).
    virtual void complete() = 0;

    // ...
};

template<typename Receiver>
struct operation : node {
    void complete() override { /* ... */ }
};
```

To correctly add cancellation support to such a data structure, the following recipe can be used:
1. Add a `bool try_cancel(node *nd)` function to the data structure that tries to remove
    `nd` from the data structure. If this succeeds, `complete()` will never be called.
    `try_cancel()` will fail (and return `false`) if the operation represented by `nd` is already being
    completed concurrently (or if it was completed in the past).
    The data structure's own synchronization mechanisms (e.g., mutexes) can be used to determine this outcome.
    For example, this can be done by adding a member to the node that distinguishes between the states
    "not submitted yet", "linked to the data structure" and "completion pending" (with the latter
    corresponding to the scenario that a call to `complete()` is either imminent or has already been performed).
2. Add an `observation_resolver` to `operation` that calls `try_cancel()` on its cancellation path
    and `async::execution::set_value()` on its resumption path.
    Implement the `complete()` override in `operation` by calling the `observation_resolver`'s
    `complete()` method.
    ```cpp
    void complete() {
        cr_.complete();
    }
    ```
3. In the operation's `start()` function, follow the following recipe:
    ```cpp
    void start() {
        bool fast_path = false;
        {
            // Lock the data structure, determine if a fast path is applicable etc.
            // ...
        }

        if (fast_path)
            return async::execution::set_value(/* ... */);
        cr_.listen(ct_); // If fast path is not taken, register cancellation_resolver.
    }
    ```
