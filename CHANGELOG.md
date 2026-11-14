# Changelog

## 0.9.0-beta.3

These changes have not been tested in depth yet, so some bugs may remain.

- Installing an optimized game over its own update no longer leaves it unable to start: the files it shares with
  that update stay when the update is removed.
- Installing an optimized game with its update under it no longer asks whether to install it again.

## 0.9.0-beta.2

These changes have not been tested in depth yet, so some bugs may remain.

- Styles in Settings › Look: NXDock, RuneStone, Vampire and Huntress, as in NXPress, in light and dark.
- An official update over a game compressed by NXDock or optimized with NXPress no longer breaks it (error
  2002-4021): before it is queued, NXDock offers to install the original game first or to leave the update out, and
  Compress games shows such a game with both ways out. Thanks to Insane91_PL (GBAtemp) for reporting it.
- The queue always installs a game before its update and its DLC, whatever order they were added in.
- Installing an optimized game removes the updates merged into it, a merged game no longer asks to download its
  update, and merging keeps the system version the update asks for. Thanks to Trickster101 (GBAtemp) for reporting it.

## 0.9.0-beta.1

The first public beta. It has not been tested in depth yet, so it may still have bugs and errors: please
report any problem you find, on the issues page or on Discord.

- Installs NSP, NSZ, XCI and XCZ from Switch Library Manager Web over the network, paired with a code, with no
  addresses or keys to type.
- Also installs from the SD card, a USB disk (FAT32 or exFAT), MTP, NS-USBloader and shops of the kind of Tinfoil and
  Ownfoil.
- Installs the extra compression of NXPress (NCZSECT2 and NCZSECT3) as it is.
- All or nothing: every part checked against its SHA-256, only what is missing downloaded, an install cut by the
  console going off goes on where it stopped; Check and Repair for installed games.
- The save of a game copied before an update, a repair or another version.
- Update all, another version of an update, undo, history, and what is missing for every game on the console.
- Files on the SD card and a USB disk: copy, move, rename, new folder and delete.
- Saves, space and dumps, a check of the SD card, cheats and mods.
- Compress games (experimental): installed games compressed, and a game merged with its update.
- Updates itself after checking the signature of the release.
- A shortcut on the HOME menu that opens NXDock with all the memory of a game.
- Made for the buttons and the touch screen, light and dark themes, 11 languages and a first-run wizard.

Details of each feature are in the [wiki](https://github.com/DeLFuS77/NXDock/wiki).
