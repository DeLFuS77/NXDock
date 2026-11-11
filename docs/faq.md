# Questions and problems

## NXDock does not find my library app

- Turn on **Install over the network** in the Settings of Switch Library Manager Web.
- The console and the app must be on the same network. A firewall on the computer may block it: allow the app.
- An app outside your home network, or behind a proxy, can be typed by hand: Settings › Address by hand.

## Installs are slow

- Open NXDock with the full memory of a game (hold R while opening a game, then choose NXDock), not from the album.
- Run the speed test in Settings: it finds how many connections at once work best on your network.
- A 5 GHz Wi-Fi or a wired adapter helps more than anything else. A slow or fake SD card slows the write stage: check
  it in More › Space and dumps › Check the SD card.

## An install failed

The queue and the history say what happened and what to do, with a code. Nothing is left half installed. If you
report it, attach `sdmc:/switch/NXDock/install.log`.

## The console went off in the middle of an install

Open NXDock again: it offers to go on where it stopped, keeping the parts already written and checked, or to discard
them.

## Does NXDock need my keys or sigpatches?

Installing does not: NXDock never reads your `prod.keys` to install, and never installs firmware or sigpatches. Only
[Compress games](compress-games.md) reads `prod.keys`, and a compressed game needs sigpatches to start.

## Is it safe to use online?

Installing homebrew and your own copies always carries a risk if the console connects to Nintendo. Use an emuMMC and
90DNS (or a DNS block); NXDock warns before the first install when it finds neither.

## Can I install official updates over a game compressed by NXDock?

Over a game compressed on its own, an official update installs as usual. Over a merged game, no: install updates with
NXDock, which merges them. See [Compress games](compress-games.md).

## Where does NXDock keep its files?

| Path | What |
|---|---|
| `sdmc:/switch/NXDock/NXDock.nro` | NXDock itself. |
| `sdmc:/config/NXDock/` | Settings, the link to the library app and the shops. |
| `sdmc:/switch/NXDock/history.tsv` | The history of installs. |
| `sdmc:/switch/NXDock/covers/` | The covers, kept after the first time. |
| `sdmc:/switch/NXDock/mods/` | Mod packages, by title ID. |
| `sdmc:/switch/NXDock/resume/` | What an install cut by the console going off left, to go on. |
| `sdmc:/switch/NXDock/compressed.txt`, `recipes/` | What Compress games did. |
| `sdmc:/switch/NXDock/*.log` | `install.log`, `link.log` and `compress.log`, for bug reports. |

Deleting `sdmc:/config/NXDock/` starts again from the wizard.

## How do I report a problem?

[Open an issue](https://github.com/DeLFuS77/NXDock/issues/new/choose) with what you did, what happened, the version of
NXDock, your firmware and Atmosphère, and the log of the screen where it happened. Questions are also welcome on
[Discord](https://discord.gg/acQVZSEBR).
