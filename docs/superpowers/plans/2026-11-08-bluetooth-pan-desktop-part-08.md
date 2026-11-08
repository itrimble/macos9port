# Desktop Bluetooth PAN plan — part 8

[Plan index](2026-11-08-bluetooth-pan-desktop.md). Producer plan text; implementation acceptance is tracked separately.

### Task 9: Wire the modules into the desktop build and document it

The desktop binary has to carry the new sources and the two libraries, and the Bluetooth panel has to register itself at startup.

**Files:**
- Modify: `Makefile`
- Modify: `config/desktop-tests.mk`
- Modify: `src/main.c`
- Modify: `README.md`

**Interfaces:**
- Consumes: `bluetooth_panel_init()` from `include/bluetooth_panel.h`.
- Produces: `build/ospi` with the Bluetooth panel, the manager and the guest network service linked in.

- [ ] **Step 1: Add the libraries to the root Makefile**

In `Makefile`, after the `STORAGE_LINK` line (line 28), add:

```make
# Guest networking (net/net.mk) and the Bluetooth stack (bluetooth/bluetooth.mk).
# The desktop owns the guest network service and the Bluetooth PAN panel.
NET_DIR ?= net
NET_OUT ?= build/net
BT_DIR ?= bluetooth
BT_OUT ?= build/bluetooth
NET_A := $(NET_OUT)/libos999net.a
BT_A := $(BT_OUT)/libos999bt.a
CFLAGS += -I$(NET_DIR)/include -I$(BT_DIR)/include -I$(BT_DIR)/src
```

- [ ] **Step 2: Link the new sources into the desktop**

In `Makefile`, in the `build/ospi` rule (line 61), add the new sources and libraries.

The prerequisites become:

```make
build/ospi: $(SHORTCUT_HOST) include/desktop_shortcuts.h $(PRINT_HOST) $(CORE) $(STORAGE_A) $(AUDIO_SRC) src/winamp_skin.c src/media_player_panel.c src/os999_startup.c src/main.c src/desktop_guest.c src/desktop_host.c src/desktop_sound.c $(HDRS) $(AUDIO_HDRS) include/winamp_skin.h include/media_player_panel.h include/os999_startup.h include/desktop_guest.h include/desktop_host.h include/desktop_sound.h $(BROWSER_SRC) $(BROWSER_OVERLAY) $(BROWSER_HDRS) src/desktop_bluetooth.c src/desktop_guestnet.c src/bluetooth_panel.c src/desktop_panel.c src/desktop_bt_none.c $(NET_A) $(BT_A)
```

and the recipe becomes:

```make
	$(CC) $(CFLAGS) $(SDL_CFLAGS) $(FFMPEG_CFLAGS) -o $@ $(SHORTCUT_HOST) $(PRINT_HOST) $(CORE) $(STORAGE_LINK) $(AUDIO_SRC) src/winamp_skin.c src/media_player_panel.c src/os999_startup.c src/main.c src/desktop_guest.c src/desktop_host.c src/desktop_sound.c $(BROWSER_SRC) $(BROWSER_OVERLAY) src/desktop_bluetooth.c src/desktop_guestnet.c src/bluetooth_panel.c src/desktop_panel.c src/desktop_bt_none.c $(NET_A) $(BT_A) $(SDL_LIBS) $(FFMPEG_LIBS) $(FT_LIBS) $(BROWSER_LIBS) -pthread -lm $(NET_LIBS)
```

- [ ] **Step 3: Use the macOS radio on macOS**

In `Makefile`, in the `ifeq ($(shell uname -s),Darwin)` block (line 39), add:

```make
BT_HOST_OBJ := build/desktop_bt_darwin.o
BT_HOST_LIBS := -framework IOBluetooth -framework Foundation
build/desktop_bt_darwin.o: src/desktop_bt_darwin.m include/desktop_bluetooth.h $(BT_DIR)/src/bt_darwin.h
	mkdir -p build
	$(CC) $(CFLAGS) -I$(BT_DIR)/src -fobjc-arc -c $< -o $@
```

and in the `else` branch (after line 60) add:

```make
BT_HOST_OBJ := build/desktop_bt_none.o
BT_HOST_LIBS :=
build/desktop_bt_none.o: src/desktop_bt_none.c include/desktop_bluetooth.h
	mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@
```

Then replace `src/desktop_bt_none.c` in the `build/ospi` rule with `$(BT_HOST_OBJ)`, add `$(BT_HOST_OBJ)` to the prerequisites and `$(BT_HOST_LIBS)` to the recipe.

The driver is always linked as an object, never as a source, because the macOS driver is Objective-C and needs `-fobjc-arc`; the object rule above compiles it that way on every build. `src/desktop_bt_none.c` is still compiled directly by the test targets, which never link the macOS driver, so the two definitions of `desktop_bt_platform_ops` never meet in one binary.

- [ ] **Step 4: Register the panel at startup**

In `src/main.c`, after the session is created, add:

```c
    bluetooth_panel_init();
```

and add `#include "bluetooth_panel.h"` at the top.

- [ ] **Step 5: Poll the link every tick**

In `src/session.c`, in `session_tick`, add:

```c
    const SessionBluetoothHooks *bt = session_bluetooth_hooks();
    if (bt && bt->poll) bt->poll();
```

- [ ] **Step 6: Build the desktop**

Run: `BUILDFLEET_LOCAL=1 make`
Expected: `build/ospi` links with no undefined symbols and no new warnings.

- [ ] **Step 7: Run the whole suite**

Run: `BUILDFLEET_LOCAL=1 make test`
Expected: every test passes.

- [ ] **Step 8: Run the sanitizers and the leak check**

Run:

```bash
make bt-asan
make -f net/net.mk net-asan
leaks --atExit -- ./build/test_desktop_bluetooth
leaks --atExit -- ./build/test_bluetooth_panel
leaks --atExit -- ./build/test_desktop_guestnet
```

Expected: every sanitizer build is clean and every leak check reports zero leaks. LeakSanitizer is not available on this platform, so `leaks --atExit` is the check.

- [ ] **Step 9: Check the file sizes**

Run:

```bash
wc -l include/desktop_bluetooth.h src/desktop_bluetooth.c src/desktop_bt_darwin.m src/desktop_bt_none.c \
  include/desktop_guestnet.h src/desktop_guestnet.c include/bluetooth_panel.h src/bluetooth_panel.c \
  include/desktop_panel.h src/desktop_panel.c tests/test_desktop_bluetooth.c tests/desktop_bt_sim.c \
  tests/test_bluetooth_panel.c tests/test_desktop_panel.c tests/test_desktop_guestnet.c
```

Expected: no file reaches 500 lines. Split a file that does.

- [ ] **Step 10: Document the flow**

In `README.md`, add a short section:

```markdown
### Bluetooth network

`999 > Bluetooth Network...` opens a panel that lists the Bluetooth peers an
inquiry found. Picking one brings up a personal area network: the guest keeps
its own address on the link (`192.168.10.1`) and reaches the peer
(`192.168.10.2`) over Bluetooth. The macOS radio uses IOBluetooth for the
inquiry and for the channel; the link itself is unverified at runtime because
no peer device was available. The panel is disabled and says so on a platform
without a radio.
```

- [ ] **Step 11: Commit**

```bash
git add Makefile config/desktop-tests.mk src/main.c src/session.c README.md
git commit -m "Wire the Bluetooth panel into the desktop"
```

---

## Self-Review

**Spec coverage**

| Spec section | Task |
|---|---|
| `bt_bnep_adopt` | 1 |
| The manager | 4 |
| The driver seam, the macOS driver, the no-radio stub | 4, 7 |
| The guest network service | 3 |
| The panel | 5 |
| The panel façade | 6 |
| The menu item, after *Tailscale*, disabled with a reason | 6 |
| Addressing: the device addresses on the link, `192.168.10.0/24`, the desktop is `.1` | 4, 7, 8 |
| The built-in policy | 3 |
| The errors: no radio, nothing found, the link fails, the peer closes the link | 4, 5, 6 |
| The tests: the adopt test, the whole flow, the panel renders | 1, 5, 8 |
| The files stay under 500 lines | 9 |
| Out of scope: an incoming PAN, the policy file, a real radio run | not in any task |

**Type consistency**

`DesktopBtDriverOps`, `DesktopBtCallbacks`, `DesktopBtDevice`, `DesktopBtManager` and every manager function are declared once in `include/desktop_bluetooth.h` and used with the same names in Tasks 4, 5, 7 and 8. `desktop_bt_platform_ops`, `desktop_bt_available` and `desktop_bt_unavailable_reason` are defined in `src/desktop_bt_none.c` for the stub and in `src/desktop_bt_darwin.m` for macOS; only one of the two is linked into `build/ospi`, and Task 9 spells that out. `bluetooth_panel_row_rect` and `bluetooth_panel_disconnect_rect` are declared in `include/bluetooth_panel.h` and used by the Task 5 test. `SessionBluetoothHooks` is declared in `include/ospi.h` in Task 5 and used by Tasks 5, 6 and 9.

**No placeholders**

Every step names the file, the function, the command and the expected result, and every code block is the code to write.
