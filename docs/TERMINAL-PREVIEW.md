# Terminal Preview — 6 November 2026

## Source and artifact

Isolated branch `codex/terminal-nerd-fonts`, based on release/1.1 at `e66a061`.
Changes are uncommitted and not merged into the concurrent desktop worktree.
Artifact: `build/OS999 Terminal Preview R2.app` (ad-hoc signed).
The local test-volume path is in `build/terminal-preview-volume.txt`.

## Implemented

- Applications/Utilities/Terminal is seeded without overwriting existing files.
  A matching built-in marker opens the host shell; unrelated same-named files
  are not reinterpreted as launchers. The existing terminal icon mapping is used.
- Dark default, 15-pixel Meslo Nerd Font Mono regular/bold, 9×20 cells,
  antialiased glyph cache, Powerline/private-use Unicode symbols and UTF-8 copy.
- Interactive login shell using the existing host configuration. Inspection found
  Oh My Zsh, Powerlevel10k and Meslo Nerd Fonts in Ian's iTerm preferences.
  No shell configuration, plugin, credential or system font was changed.
- Fonts, source URL/hash and upstream license notices are bundled with the app.
- `--terminal` opens a terminal on launch for focused testing.

## Validation and observed failure

`BUILDFLEET_LOCAL=1 make -j2 test all` passed after updating the desktop seed
expectation for the new Utilities folder. The first run caught that outdated
one-item Applications assertion. The earlier resize assertion was also adjusted
for the larger line spacing. These failures were resolved, not suppressed.

After the final shell-working-directory adjustment, the desktop and R2 package
rebuilt, `build/test_terminal` passed 151 checks, and strict deep code-signature
verification passed. The complete suite was not repeated after this final
working-directory-only adjustment. Terminal checks cover real PTY resize/reaping,
isolated zsh login/interactive options and rc loading, launcher activation,
font glyph coverage, antialiasing, clipping, colors and paste safety.

The first GUI preview started in Home. Ian's existing zsh hooks redirected it
to `/Volumes/nas/projects`; a sampled shell stack showed directory opening in
a pre-prompt hook, and no usable prompt appeared. That preview was closed.
R2 starts in its local OS999 volume. A real-window observation showed the
Powerlevel10k path prompt, right-side clock/checkmark, dark background and
rendered symbols. This observation is not an interactive NAS SSH login result.

## Limits and integration

This remains a fixed-cell terminal, without full combining-character/CJK width
handling, emoji shaping or complete terminal protocol coverage. Without FreeType,
only limited bitmap fallback text is available. Bundling a font is not proof of
HiDPI rendering. SSH uses the host executable, keys and authentication behavior.

Keep the active desktop-integration worktree intact. In particular, preserve its
additional focus/app-switching line in `terminal_open_custom` when integrating
this change. Shared Makefile, volume, session and main-loop changes require
reconciliation; do not replace those files wholesale with this preview's copies.

## Tailscale utility — 6 November 2026

The same isolated preview adds a compiled `os999-tailnet` helper. Open
Applications/Utilities/Tailscale or OS999 → Tailscale. This is an interactive
terminal utility, not a separate graphical settings panel. It uses the installed
host Tailscale client; no daemon, login keys, auth keys or VPN implementation are
bundled. The client must be installed and authorized for the current user.

The device list shows hostnames, online status and validated IPv4/IPv6 addresses.
`ssh NUMBER USER` uses the last displayed list and launches `/usr/bin/ssh` with
separate arguments, preserving host-key checking and normal authentication.
Offline devices and invalid usernames are rejected. `refresh` updates numbering.
The utility does not enable a Tailscale SSH server or modify remote permissions.

`settings` reads DNS, route acceptance and incoming-connection blocking only.
`dns`, `routes`, `shields`, `connect`, `disconnect` and `login` require a literal
`yes` after explaining the host-wide effect. These actions can affect other
applications and ongoing remote connections. A command failure is displayed;
OS999 never automatically runs sudo. The login command uses Tailscale's own flow.

The JSON reader limits response size, nesting depth, token count and peer count.
Device metadata is sanitized before terminal display, and addresses are parsed
with `inet_pton`; it never interpolates a device name into a shell command.
Read-only client calls have an eight-second deadline. Unsupported responses fail
visibly. Tailscale's JSON/debug preference interfaces can change across releases.

`make test-tailnet` uses a temporary fake CLI and checks discovery, IPv6,
cancellation, each settings command, offline/invalid SSH input, hostile metadata,
malformed and oversized JSON, nonzero client exits and timeouts. It does not
alter the host's real settings. The real `--status` probe on this Mac returned
Running and seven peers, including an online NAS. The installed client was
1.104.4; upgrading it was deferred to avoid interrupting active network sessions.
Authenticated NAS SSH, browser login completion, real disconnect/reconnect,
real settings changes and Linux execution remain unverified.

### Packaged verification

At 18:46 CST on 6 November 2026, `OS999 Tailscale Preview.app` was launched
with a new temporary local volume and `--tailscale`. Its real window visibly
showed all seven devices, the NAS online at number 2, the available commands
and the input prompt. This was a rendering/startup check, not SSH authentication.
The full `BUILDFLEET_LOCAL=1 make -j2 test all` run exited 0, as did
`make test-tailnet` and strict deep code-signature verification. The build emitted
linker alignment warnings. Logs are `/tmp/os999-tailnet-full.log` and
`/tmp/os999-tailnet-package.log`; source/artifact hashes are recorded in
`build/tailnet-preview-receipt.json`. No host settings were changed during testing.
