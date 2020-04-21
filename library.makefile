
.PHONY: install
install:
	mkdir -p $(DESTDIR)$(prefix)include/async
	install --mode=0644 $S/include/async/basic.hpp $(DESTDIR)$(prefix)include/async/
	install --mode=0644 $S/include/async/cancellation.hpp $(DESTDIR)$(prefix)include/async/
	install --mode=0644 $S/include/async/doorbell.hpp $(DESTDIR)$(prefix)include/async/
	install --mode=0644 $S/include/async/execution.hpp $(DESTDIR)$(prefix)include/async/
	install --mode=0644 $S/include/async/jump.hpp $(DESTDIR)$(prefix)include/async/
	install --mode=0644 $S/include/async/mutex.hpp $(DESTDIR)$(prefix)include/async/
	install --mode=0644 $S/include/async/result.hpp $(DESTDIR)$(prefix)include/async/
	install --mode=0644 $S/include/async/queue.hpp $(DESTDIR)$(prefix)include/async/

