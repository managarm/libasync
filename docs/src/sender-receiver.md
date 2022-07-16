# Senders, receivers, and operations

libasync is in part built around the concept of senders and receivers. Using senders
and receiver allows for allocation-free operation where otherwise an allocation
would be necessary for the coroutine frame. For example, all of the algorithms
and other asynchronous operations are written using them.

## Concepts

### Sender

A sender is an object that holds all the necessary arguments and knows how to
start an asynchronous operation. It is moveable.

Every sender must have an appropriate `connect` member method or function overload
that accepts a receiver and is used to form the operation.

### Operation

An operation is an object that stores the necessary state and handles the actual
asynchronous operation. It is immovable, and as such pointers to it will remain
valid for as long as the operation exists. When the operation is finished, it
notifies the receiver and optionally passes it a result value.

Every operation must either have a `void start()` or a `bool start_inline()` method
that is invoked when the operation is first started. `void start()` is equivalent to
`bool start_inline()` with `return false;` at the end.

#### Inline and no-inline completion

Operations that complete synchronously can signal inline completion. If an operation
completes inline, it sets the value using `set_value_inline`, and returns `true` from
`start_inline` (Operations that have a `start` method cannot complete inline). Inline
completion allows for certain optimizations, like avoiding suspending the coroutine
if the operation completed synchronously.

### Receiver

A receiver is an object that knows what to do after an operation finishes (e.g. how to
resume the coroutine). It optionally receives a result value from the operation.
It is moveable.

Every receiver must have `void set_value_inline(...)` and `void set_value_noinline(...)`
methods that are invoked by the operation when it completes.
