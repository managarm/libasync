# execution

```cpp
#include <async/execution.hpp>
```

The following objects and type definitions are in the `async::execution` namespace.

This header contains customization point objects (CPOs) for the following
methods/functions:
 - `connect` (as a member or function),
 - `start` (as a member or function),
 - `set_value` (as a member),

In addition to that, it provides a convenience type definition for working with operations:
```cpp
template<typename S, typename R>
using operation_t = std::invoke_result_t<connect_cpo, S, R>;
```

## Examples

```cpp
auto op = async::execution::connect(my_sender, my_receiver);
async::execution::start(op);
```
