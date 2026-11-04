# OS Pi

Clean-room desktop that looks like a classic Mac, for arm64 and x86_64.
It is a userspace program. It is not an OS kernel and not an emulator.

## Do not add

- Apple ROMs, System files, or a Toolbox built by decompiling Apple code
- Chicago, Charcoal, Geneva, or the Apple logo
- SheepShaver, Basilisk II, Mini vMac, or any wrapper that needs a ROM
- Copied text from Inside Macintosh

The on-screen name is OS Pi. The menu mark is the Greek letter pi.

## Try it on this Mac

```bash
make test
make run
```

`make run` builds `build/OS Pi.app` and opens it. The volume is
`~/Library/Application Support/OS Pi/Volume`. On Linux the same sources
build with SDL2; `make run` starts `./build/ospi --volume ./volume`.
That Linux path has not been executed on a Raspberry Pi from this tree yet.

Files stay under 500 lines. Tests are headless and do not need SDL.
