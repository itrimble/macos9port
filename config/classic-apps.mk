# Install Classic Apps: catalogue, verified install pipeline, dialog. Headless test only.
build/test_classic_apps: tests/install_network_stub.c $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_classic_apps.c $(HDRS) $(STARTUP_HDR)
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(filter-out src/modern_network.c,$(CORE) $(STORAGE_LINK)) $(STORAGE_LINK) tests/install_network_stub.c $(STARTUP_SRC) tests/test_classic_apps.c $(FT_LIBS) -pthread $(if $(filter Darwin,$(shell uname -s)),,-ldl) -lm

test: build/test_classic_apps

build/test_classic_progress: tests/install_network_stub.c $(CORE) $(STORAGE_A) $(STARTUP_SRC) tests/test_classic_progress.c $(HDRS) $(STARTUP_HDR)
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ $(filter-out src/modern_network.c,$(CORE) $(STORAGE_LINK)) $(STORAGE_LINK) tests/install_network_stub.c $(STARTUP_SRC) tests/test_classic_progress.c $(FT_LIBS) -pthread $(if $(filter Darwin,$(shell uname -s)),,-ldl) -lm

test: build/test_classic_progress
