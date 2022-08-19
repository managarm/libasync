# wait-group

This header includes utilities related to the `wait_group` primitive.

```cpp
#include <async/wait-group.hpp>
```

Wait groups are synchronization primitives that wait for a counter to reach
zero. This counter is conceptually bound to a group of related work that the
consumer would like to simultaneously wait for (hence the name wait groups).
