# Extensions subsystem: scanner, persisted state, INIT runner, manager panel.
build/test_ext_scan: tests/test_ext_scan.c tests/ext_fixture.h src/ext_scan.c src/ext_state.c include/ext_registry.h
	$(CC) $(CFLAGS) -o $@ tests/test_ext_scan.c src/ext_scan.c src/ext_state.c

build/test_ext_state: tests/test_ext_state.c tests/ext_fixture.h src/ext_scan.c src/ext_state.c include/ext_registry.h
	$(CC) $(CFLAGS) -o $@ tests/test_ext_state.c src/ext_scan.c src/ext_state.c

build/test_ext_init: $(RUNTIME_OBJ) tests/test_ext_init.c tests/ext_fixture.h $(RUNTIME_HDRS) $(CPUOBJ)
	$(CC) $(CFLAGS) $(CPUFLAGS) $(RUNTIME_OBJ) tests/test_ext_init.c $(CPUOBJ) -lm $(FT_LIBS) -o $@

build/test_cdev_extensions: $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_cdev_extensions.c tests/ext_fixture.h $(HDRS) $(STARTUP_HDR)
	$(CC) $(CFLAGS) -Isrc -o $@ $(CORE) $(STORAGE_LINK) $(STARTUP_SRC) tests/test_cdev_extensions.c $(FT_LIBS) -lm $(NET_LIBS)

.PHONY: test-extensions
test-extensions: build/test_ext_scan build/test_ext_state build/test_ext_init build/test_cdev_extensions
	./build/test_ext_scan
	./build/test_ext_state
	./build/test_ext_init
	./build/test_cdev_extensions

test: test-extensions
