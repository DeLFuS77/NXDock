# Changelog

## 0.9.0-beta.4

These changes have not been tested in depth yet, so some bugs may remain.

- + opens the menu of actions on every screen: In the console, the queue, the shops, the page of a game, Space and dumps
  and Compress games. − opens it too. Each row shows the button that does the same.
- Mark all and Clear the marks wherever there are boxes to tick.
- Deleting a game from In the console can copy its saves first, to the library or to the SD card, as Settings › Saves
  says.
- The copy of the saves before an update, a repair, a removal or going back now goes to the SD card when no library is
  linked, and takes the console's and BCAT saves too, not only each user's.
- A restore that fails half way puts the save back as it was.
- In the console: the page of a game checks it and repairs it from the library from its + menu.
- In the console: Play opens a game from its page; inside another game NXDock asks first, as it closes.
- In the console: the play time, how many times a game was opened and when it was last played.
- In the console: + sorts the list by name, size, last played, play time or where it is; the page shows the title ID.
- In the console: updates and DLC without their game show in the list, with a filter, and their page can install the game.
- Space › Tickets: every ticket with its game; those without a game can be deleted, saved to the SD card first.
  Clean leftovers also takes the tickets without a game, common and personal, each .tik saved to the SD card first.
- Deleting a game from In the console can also remove its mods and its ticket.
- Install over USB from DBI's dbibackend too, besides NS-USBloader: NXDock finds which one is on the computer.
- NSZ files made with a long window (up to 128 MiB) also install when NXDock is opened from the Album, and lack of
  memory is shown as such.
- Content made for Switch 2 is refused before anything of it is installed.
- Installs write to the SD card in larger pieces when NXDock runs as an applet.
- The install log notes the memory NXDock has at the start and at the end of each job.
- MTP: each game in Installed games has a Mods & Cheats folder, its folder in atmosphere/contents, to copy mods
  into, rename and delete.
- MTP: a save zip dropped back on its game in Saves restores it; the save it replaces is copied to the SD card first.
- MTP: files over 4 GB copied to a FAT32 SD card are kept in parts, so the console reads them as one file.
- When an install does not fit on the SD card, a line under the destination offers the internal memory for that
  install only.
- The file explorer shows which games are installed, which updates and DLC are for installed games, and which files
  are older than what is installed.
- Game card: a tile in More while a card is inserted installs its game, update and DLC.
- Update installed games from here, in the + menu of a folder and of a shop: the newer updates and DLC found there,
  all ticked, go to the queue.
- Invert marks in the + menu of every list with boxes to tick.
- Text files (.txt, .log, .ini, .json, .cfg, .md) open read only in the explorer, up to 4 MB.
- In the console: + plays a random game.
- A .nro opens from the explorer after a question; what the queue had can carry on when NXDock opens again.
- The text viewer keeps Wrap lines for the next time.
- MTP and NS-USBloader: once the cable is in, a chip shows its speed. At USB 2.0 it offers USB 3.0: the line goes in
  system_settings.ini with a copy kept first, and the console reboots when nothing is installing.
- Settings: the copy of the saves and the automatic copies have a group of their own, Saves.
- Settings: the panel on the right says what the row in focus does and its value now; the rows of the network keep
  the speed test.

## 0.9.0-beta.3

These changes have not been tested in depth yet, so some bugs may remain.

- Installing an optimized game over its own update no longer leaves it unable to start: the files it shares with
  that update stay when the update is removed.
- Installing an optimized game with its update under it no longer asks whether to install it again.
- + opens the menu of actions of each screen, and no longer closes NXDock: the HOME button does that.
- Every tab of the top menu shows its icon and its name when they fit.
- The first action of a file or folder says only Install.
- A game the library sends without a name or a cover no longer shows only a question mark.

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
