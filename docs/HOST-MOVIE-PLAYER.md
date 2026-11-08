# Native movie player

The desktop registers a native FFmpeg video backend and reuses the existing shared audio service. The C functions `NewMovieFromFile`, `StartMovie`, `GetMovieTime` and related helpers are host APIs with Movie Toolbox-like names. They do not implement Apple's guest QuickTime ABI, 68K traps, CFM imports, or proprietary codecs.

Finder opens MOV, QT, MP4 and M4V files in QuickTime Player 999. Controls include play/pause, a seek bar, volume, elapsed/total time, Space, and five-second Left/Right seeks. Dropping one movie from a Finder window onto an open player opens that document without moving it. Host operating-system file drops are not implemented.

Video is streamed from a local regular file with a 2 GB limit. Images are bounded to 1920x1080 and duration to 24 hours; external MOV data references are disabled. Codec availability comes from the installed FFmpeg build. The older raw movie path remains available when no modern backend is registered. Audio uses the existing shared output and its playback clock when available; unavailable audio decoding/output can leave video silent.

On macOS the decoder attempts VideoToolbox. Software formats are selected when hardware is unavailable; a later hardware decoder or transfer failure is reported, not automatically retried. Pixel conversion and desktop composition remain on the CPU. This host's generated H.264 test used software frames after VideoToolbox initialization failed.

`make test-quicktime` generates small local H.264/AAC, QuickTime Animation 32/8-bit, and Video 16-bit fixtures using FFmpeg, then checks changing decoded frames, timing, seek, pause, EOF, volume, malformed files, and symlink rejection. AAC is checked through mixed PCM with dummy SDL output, not listening. The ordinary movie-window tests exercise modern controls, painting and Finder drops using an isolated backend fixture. No proprietary media is committed.

Desktop compilation and headless checks are separate from visible player acceptance, audible output, accelerated decoding, Linux/mobile toolchain builds and guest application compatibility. Those latter checks remain unverified.
