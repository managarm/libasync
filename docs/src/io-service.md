---
short-description: The IO service
...

# IO service

The IO service is an user-provided class which manages waiting for events and
waking up coroutines/operations based on them.

The IO service must provide one method: `void wait()`. This method is called when
there is no more work to do currently. It waits for any event to happen, and wakes
up the appropriate coroutine/operation which awaited the event.

**Note:** `async::run` and `async::run_forever` (see [here](headers/basic/run.md#prototype))
take the IO service by value, not by reference.

## Example

The following example shows the approximate call graph executing an event-loop-driven coroutine would take:

 - `async::run(my_sender, my_io_service)`
   - `my_operation = async::execution::connect(my_sender, internal_receiver)`
   - `async::execution::start_inline(my_operation)`
     - `my_operation` starts running...
       - `co_await some_ev`
         - `some_ev` operation is started
           - `my_io_service.add_waiter(this)`
   - (`async::execution::start_inline` returns `false`)
   - `my_io_service.wait()`
     - IO service waits for event to happen...
     - `waiters_.front()->complete()`
       - `some_ev` operation completes
         - `my_operation` resumes
           - `co_return 2`
             - `async::execution::set_value_noinline(internal_receiver, 2)`
   - `return internal_receiver.value`
 - (`async::run` returns `2`)
