# Contributing

Thanks for wanting to help. NXDock is free software under the [GNU GPL-3.0](LICENSE) (or any later version);
contributions are accepted under the same license.

## Reporting bugs and asking for features

- Open an issue on <https://github.com/DeLFuS77/NXDock/issues> with what you did, what you expected and what
  happened, the version of NXDock (Settings), the firmware and Atmosphère version of the console, and where you were
  installing from. The code under an error and the end of `sdmc:/switch/NXDock/install.log` help a lot.
- [Reporting a problem](https://github.com/DeLFuS77/NXDock/wiki/Reporting-a-problem) in the wiki lists what to send
  and where the logs are.
- Security problems are reported privately: see [SECURITY.md](SECURITY.md).
- Never attach `prod.keys`, `title.keys`, the key of a paired console or game files.

## Contributing code

1. Fork the repository and make a branch for your change.
2. Build and test it locally. You need Docker; both scripts run in the official `devkitpro/devkita64` container:

   ```sh
   tools/build.sh   # builds build/NXDock.nro
   tools/test.sh    # the tests that need no console
   ```

   The interface is in `source/` (`app.cpp`, `ui.cpp` and the screens of More in `more_*.cpp`), drawn with deko3d and
   nanovg. The install core is yati from Sphaira, in `source/yati` and `include/yati`, changed for NXDock where it is
   marked `NXDock:`; `compat/` is the layer that takes the place of the rest of Sphaira. The texts of the interface
   are in `source/i18n.cpp` and `source/i18n_more.cpp`. Compress games is in `compress_nx.cpp` (on the console) and
   in plain C++ tested on the computer: `romfs_lz4` and `lz4nintendo` (the LZ4 layer), `patch_view` (a game read
   with its update), `merge_pack`, `merge_nca`, `merge_game` and `merge_meta` (the merged game) and `merge_recipe`
   (newer updates). The file list is `more_files.cpp` and `fileops.cpp`, updating itself `more_update.cpp` and
   `minisig.cpp`, MTP `mtp_*.cpp`, and the HOME menu shortcut `forwarder.cpp` with its loader in `hbl/`.

3. Open a pull request against `main`, saying what it changes and why. The build, the tests and CodeQL must pass
   before it is merged. Small fixes are welcome directly; for a larger change, open an issue first to talk about it.

## Requirements for a change

- **Tests:** code that needs no console (parsers, the network source, the queue planner...) comes with a test in
  `tests/`, added to `tools/test.sh`.
- **Controller and touch:** every screen works with the buttons and with the touch screen, with targets large enough
  for a finger.
- **Interface texts:** every text is translated in the 11 languages of NXDock.
- **Safety:** an install stays all or nothing, the internal memory is only used after a question, and NXDock never
  installs firmware or sigpatches.
- **Changelog:** user-facing changes get a line under `## Unreleased` in [CHANGELOG.md](CHANGELOG.md).
- **No keys nor game data** in commits, tests or issues.

## Releases

Versions follow [Semantic Versioning](https://semver.org/) and are tagged `MAJOR.MINOR.PATCH` (without a "v"), the
version of `CMakeLists.txt`. A tag builds `NXDock.nro` and the zip for the SD card, with their checksums and a signed
provenance, as a draft; the maintainer signs the checksums on their own computer (`go run ./tools/releasesign <tag>`)
and publishes it.
