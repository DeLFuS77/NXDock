# NXDock

A homebrew installer for Switch that installs your own NSP, NSZ, XCI and XCZ
copies from your [Switch Library Manager Web](https://github.com/DeLFuS77/switch-library-manager-web)
library over the network.

**Status: early work.** This build only checks that NXDock starts; it does not
install anything yet.

## Goals

- Its own interface: your library as a grid of covers, a page per game, a queue
  that shows every stage of the install.
- Fast installs: download, decompress and write at the same time, on several
  threads, with a SHA-256 check of every part.
- The formats of NXPress, NCZSECT2 and NCZSECT3 included, installed natively.
- Pair with the library app with a code: no keys typed by hand.
- SD card by default; the internal memory only after you confirm.

For your own games and your own consoles only.

## Building

You need Docker. From the repository root:

```sh
tools/build.sh
```

This builds `build/NXDock.nro` inside the official `devkitpro/devkita64`
container. Copy it to `sdmc:/switch/` on the SD card.

## Licence

GPL-3.0-or-later. See [LICENSE](LICENSE) and [NOTICE](NOTICE) for the credits
of the third-party code it uses.
