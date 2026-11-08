# Host automation scope

OS999 provides a clean-room native scripting component and event bridge. This is not Apple's OSA binary interface, AppleScript implementation, or guest Apple Event trap support.

The small textual dialect accepts `get PROPERTY`, `set PROPERTY to "VALUE"`, `shut down`, and `restart`, with blank lines and `--` comments. A program contains at most 16 statements. It has no variables, expressions, `tell` blocks, external applications, shell commands, or Apple compiled-script format.

The desktop backend exposes `name`, `version`, and `theme` queries; only `theme` is writable. Shutdown quits OS999 and restart restarts its Finder session. Neither shuts down the host.

Local IPC is disabled by default. Set `OS999_AUTOMATION_SOCKET` to an unused absolute socket path inside an existing owned 0700 directory. Clients bind their own private Unix datagram reply sockets. Each request is one JSON object, for example:

```json
{"class":"core","event":"getd","property":"theme"}
```

`core/setd` also requires a string `value`. `misc/shut` accepts a boolean `restart`. The optional `target` must be empty or `os999`. Unknown well-formed members are ignored for forward compatibility; duplicate keys, invalid types, malformed JSON and unsupported events are rejected. Strings are bounded and restricted to the currently supported ASCII representation.

Validation: `make test-applescript`, boundary and private-socket session tests included in `make test`, desktop compilation, and AddressSanitizer/UndefinedBehaviorSanitizer checks. Tests use memory-only preferences and a stub desktop-command receiver. No external application or guest AppleScript compatibility has been tested.
