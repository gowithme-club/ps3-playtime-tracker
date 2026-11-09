# Repository Guidelines

Canonical instructions for any agent working in this repository. `CLAUDE.md` only points here.

## What this is

A resident VSH plugin for a jailbroken PS3 (CFW / PS3HEN): `plugin/gwm.sprx`, loaded into `vsh.self` at boot, which detects the running game and pushes play activity to a gwm-stats server. PS3 unloads homebrew applications when a game starts, so a plugin inside `vsh.self` is the only shape that can keep running — that constraint is the reason for most of what follows.

Alongside it, `app/` is an ordinary homebrew status screen (PSL1GHT, packaged as a PKG) that renders the plugin's status file on the TV.

`plugin/version.h` (`GWM_VERSION`) is the single place the version lives; the Makefile reads it from there and names signed builds `dist/gwm-<version>.sprx`. Bump it whenever on-console behaviour changes — rolling back costs a console reboot, which is why every working build is kept.

## Sibling repositories

This plugin is one client of a server that owns all the domain logic — dedup, sessions, trophies, live status, dashboard. A client only forwards events over the ingest API; anything that changes server behaviour or the contract itself belongs in gwm-stats, not here.

- gwm-stats — server, ingest endpoints, web dashboard, public read API. The contract is `docs/ingest-api-v1.md`, with a client-author version at `docs/ingest-api-v1-client.md`.
- [switch-playtime-tracker](https://github.com/gowithme-club/switch-playtime-tracker) — Atmosphere sysmodule for a CFW Switch, a second client of the same ingest API. Same flows on a different console, so it is the place to compare how heartbeat, closed sessions and trophies are supposed to behave.
- gwm-stats-ios — SwiftUI app that reads the server's public API; it never writes to ingest.

## Documents and language

`README.md` (English) and `README.ru.md` are the short public pages for people installing the plugin — keep them in step with each other and with the site's setup guide. `docs/DEVELOPMENT.ru.md` is in Russian and is where the reasoning lives — what was measured on the console, what was rejected and why. Read the relevant section before changing behaviour it describes, and update it in the same commit. `CHANGELOG.md` records every version that was verified on a live console; `docs/INSTALL.md` is the install procedure. Code comments and commit messages in this repository are Russian.

## Build

Two toolchains, because one is not enough:

- **PSL1GHT / ps3dev** (open, native on macOS arm64) — the installer PKG and the `app/` status screen. `make toolchain` installs it into `~/ps3dev` from ps3dev nightlies; `make tools-check` prints what landed and what is missing. `make -C app pkg` builds the app.
- **Cell SDK** (proprietary, not in the repo, expected at `~/ps3sdk/cell`) — the plugin itself. PSL1GHT cannot produce a PRX at all: no `-mprx`, no `ecrti.o`/`ecrtn.o`, no vsh export stubs. The table in `docs/DEVELOPMENT.ru.md` settles this; do not re-open it.

The SDK's host binaries exist only as win32 PE32 executables with relocations stripped, so they must load at `ImageBase=0x400000`. Under user-mode emulation (Rosetta, qemu-user) that address is taken and wine dies with `c0000018`. A real x86_64 VM is the fix:

```bash
make vm      # colima x86_64 VM with wine + SDK (first run is slow)
make sprx    # compile in the VM, sign on the host, copy to dist/
make signing-tools   # scetool built from source into vendor/
```

`vendor/` holds scetool and console keys pulled by script and is gitignored — never commit it. There is no CI and there cannot be one: the SDK is proprietary. Release PKGs are built by the maintainer.

## Tests

`make test-host` compiles `tests/*.c` with plain `cc` and runs them on the workstation — game detect, record, HTTP, clock, trophy parsing, trophy cache, offline time, SHA-1, XMB message texts, `gwm.cfg` parsing, autostart line building. This is the only check that exists without a console, so when a bug is reproducible in pure logic, add a host test for it instead of spending a console run.

`tools/mock_ingest.py` is a stub server implementing the client contract; it answers `200` and prints every request. Point `gwm.cfg` at the workstation's IP and port to see exactly what the console sends.

## Verifying on the console

Verified hardware is 4.92 HFW with PS3HEN 3.5.0.

**Never put a new build into `boot_plugins.txt` first.** The plugin runs inside `vsh.self`, so a crash is a wedged XMB — and FTP is served by webMAN, which lives there too, leaving no way in. Load it temporarily through webMAN instead (`loadprx.ps3` / `unloadprx.ps3`, see `docs/INSTALL.md`); the rollback is a power cycle.

What the plugin leaves on the console:

- `/dev_hdd0/tmp/gwm/status.txt` — plugin state as `key = value` pairs, rewritten every cycle.
- `/dev_hdd0/tmp/gwm/queue.jsonl` — the undelivered-events queue; it survives offline stretches and power-offs, and is cleared after a `200`.
- `/dev_hdd0/tmp/gwm/app.log` — the status app's own log (the app has no stdout and no debugger).
- `/dev_hdd0/plugins/gwm.cfg` — token, optional explicit `account`, `popups`, `autostart`, host and port. Never commit a real token; the file is gitignored.
- `/dev_hdd0/autoexec.bat` — **not the plugin's file.** It appends its own `loadprx` line there after the first `200`, so the install procedure no longer asks anyone to edit a system file by hand (`plugin/autostart.h`). Other plugins live in that file: append, never replace, do nothing if the line is already there, and leave a file too large for the buffer alone. `autostart = false` opts out — and is what stops the line coming back when someone removed it by hand after a crash.

## Invariants that cost a console hang to learn

- `module_start` runs as a thread inside vsh and must end through `_sys_ppu_thread_exit`. Returning a value hangs the XMB. The frame follows `wm_vsh_menu` from webMAN MOD (GPLv3) deliberately, not stylistically.
- The poll thread gets a `0xC000` stack and `0xA00` priority, matching webMAN's `poll_thread`. `gameInfo` goes deep into paf; the 8 KB that suffices for a vsh_menu overlay hangs the console on the first poll.
- The `account` field must be identical in heartbeat, sessions and trophies — the server's dedup key includes it, so two spellings become two people. Explicit `account` from `gwm.cfg` wins, otherwise `localusername` of the profile the data belongs to.
- Ingest is append-only and at-least-once; retries are free, deletions do not exist. `GET /ingest/v1/sync` and watermark backfill are deliberately not implemented — the on-disk queue plays that role. See the section in `docs/DEVELOPMENT.ru.md` before adding them.
- Trophy sets are polled by `stat` on (size, `mtime`) each 10 s cycle, and at most one changed set is parsed per cycle: a full sweep at once blinds game detection for over a minute. Marks live in memory, so a plugin restart re-parses everything, which is how trophies earned while it was off get picked up.
- Session length is measured, never inferred. PS3 keeps no playtime history, and `docs/DEVELOPMENT.ru.md` lists what was searched and found empty — do not backfill invented durations.

## Workflow

Commits land straight on `main`; the history has no merge commits.
