<p align="center">
  <a href="https://gowithme.club"><img src=".github/assets/banner.png" alt="PS3 playtime tracker by gowithme.club: sessions, hours and trophies from a PS3 on HEN or CFW" width="100%"></a>
</p>

<p align="center">
  <b>English</b> · <a href="README.ru.md">Русский</a>
</p>

<p align="center">
  <a href="https://github.com/gowithme-club/ps3-playtime-tracker/releases/latest"><img alt="Latest release" src="https://img.shields.io/github/v/release/gowithme-club/ps3-playtime-tracker?style=flat-square&labelColor=131C16&color=C7F547&label=release"></a>
  <img alt="PS3 on HEN or CFW" src="https://img.shields.io/badge/PS3-HEN%20%7C%20CFW-C7F547?style=flat-square&labelColor=131C16">
  <a href="LICENSE"><img alt="MIT license" src="https://img.shields.io/badge/license-MIT-C7F547?style=flat-square&labelColor=131C16"></a>
  <a href="https://gowithme.club"><img alt="gowithme.club" src="https://img.shields.io/badge/profile-gowithme.club-C7F547?style=flat-square&labelColor=131C16"></a>
</p>

# PS3 playtime tracker

A VSH plugin for a PS3 on HEN or CFW that records what you play and for how
long, picks up the trophies you unlock and sends it all to your profile on
[gowithme.club](https://gowithme.club), the cross-platform gaming stats tracker.
It lives in the system shell, survives a game launch and needs no PSN account.

The source is open so you can see exactly what runs on your console and what
leaves it.

## What you get

- **Playtime per game**: disc, HDD or PSN, every session with its start, end and
  length.
- **Trophies**: read from the profile on the console, so they show up even
  without PSN.
- **Playing now**: friends see what you are playing, and the XMB tells you how
  long you played when you quit a game.
- **Offline-safe**: the report queue is kept on the HDD and survives being
  offline or powered off.
- **Starts by itself**: after the first successful run the plugin adds itself to
  autoload.
- **One profile for every console**: PS3, PS5, Switch, Steam, Xbox and PSN in one
  place on gowithme.club.

## How it works

```text
PS3 ── gwm.sprx inside the XMB detects the game and reads trophy files
         │
         └── HTTP ──► gowithme.club ──► your profile, friends, Telegram reports
```

The plugin only reports raw events. Game names, artwork, totals and
deduplication are handled by the gowithme.club server.

## Install

You need a PS3 on CFW, or on HFW with PS3HEN, with webMAN installed, a computer
with an FTP client for the setup, and a free [gowithme.club](https://gowithme.club)
account. The step-by-step guide lives in **Settings → PS3 (CFW)** on gowithme.club;
in short:

1. On gowithme.club issue a token for the console and download both files:
   `gwm.sprx` and `gwm.cfg`, which already contains the token.
2. Copy both files over FTP to `/dev_hdd0/plugins/`.
3. Wait for the XMB, then give it a couple of minutes more, and open
   `http://<console-ip>/loadprx.ps3?prx=/dev_hdd0/plugins/gwm.sprx` in a browser.
4. A notification with the plugin version and your profile name appears in the
   corner of the XMB. From then on the plugin loads with the console by itself.

The plugin is also on the [releases page](https://github.com/gowithme-club/ps3-playtime-tracker/releases/latest).
Without a token in `gwm.cfg` it makes no network requests at all. If something
goes wrong, see [docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md).

## What is sent

The game's title ID, name and version, where it was launched from, session
length, unlocked trophies, the console profile name, and the firmware and plugin
versions. The console is identified by a salted hash: the PSID and IDPS never
leave it.

## Tested on

PS3 on 4.92 HFW with PS3HEN 3.5.0. Verified versions and what was checked on a
live console are listed in [CHANGELOG.md](CHANGELOG.md). Reports from CFW
consoles and other firmware are welcome.

## Build from source

The plugin is a PRX, which the open PSL1GHT toolchain cannot produce, so the
build also needs Sony's Cell SDK 4.75. It is proprietary and not included here.

```bash
make toolchain     # open ps3dev toolchain into ~/ps3dev
make tools-check
make sprx          # needs the Cell SDK in ~/ps3sdk/cell
```

Which SDK packages are needed, the build host, signing, and what was measured on
the console: [docs/DEVELOPMENT.ru.md](docs/DEVELOPMENT.ru.md) (in Russian).

## Other consoles

| Console | Repository | Runs as |
| --- | --- | --- |
| PS5, jailbroken | [ps5-playtime-tracker](https://github.com/gowithme-club/ps5-playtime-tracker) | payload |
| PS3 on HEN or CFW | **ps3-playtime-tracker** (this one) | VSH plugin |
| Switch with Atmosphere | [switch-playtime-tracker](https://github.com/gowithme-club/switch-playtime-tracker) | sysmodule |

Stock consoles, Steam, Xbox and Discord connect on
[gowithme.club](https://gowithme.club) without installing anything.

## Help and contributing

Questions and bug reports: [Telegram support](https://t.me/gowithme_club_support)
or [GitHub issues](https://github.com/gowithme-club/ps3-playtime-tracker/issues).
Pull requests are welcome.

## License

[MIT](LICENSE).

This is unofficial homebrew, not affiliated with or endorsed by Sony Interactive
Entertainment. PlayStation and PS3 are trademarks of their respective owners.
Running homebrew on your console is at your own risk.
