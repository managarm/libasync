
.PHONY: install
install:
	mkdir -p $(DESTDIR)$(prefix)include/async
	install -p --mode=0644 $S/include/async/basic.hpp $(DESTDIR)$(prefix)include/async/
	install -p --mode=0644 $S/include/async/cancellation.hpp $(DESTDIR)$(prefix)include/async/
	install -p --mode=0644 $S/include/async/execution.hpp $(DESTDIR)$(prefix)include/async/
	install -p --mode=0644 $S/include/async/mutex.hpp $(DESTDIR)$(prefix)include/async/
	install -p --mode=0644 $S/include/async/oneshot-event.hpp $(DESTDIR)$(prefix)include/async/
	install -p --mode=0644 $S/include/async/post-ack.hpp $(DESTDIR)$(prefix)include/async/
	install -p --mode=0644 $S/include/async/recurring-event.hpp $(DESTDIR)$(prefix)include/async/
	install -p --mode=0644 $S/include/async/result.hpp $(DESTDIR)$(prefix)include/async/
	install -p --mode=0644 $S/include/async/queue.hpp $(DESTDIR)$(prefix)include/async/
	install -p --mode=0644 $S/include/async/algorithm.hpp $(DESTDIR)$(prefix)include/async/
	install -p --mode=0644 $S/include/async/promise.hpp $(DESTDIR)$(prefix)include/async/

