# Changelog

## Unreleased

- Shared with the library, when allowed: the firmware, Atmosphère, emuMMC, free space and sigpatches of the console,
  and how long each game was played.
- Settings › Help: Send a report to the library. The logs, the history and the crash reports go to Switch Library
  Manager Web as a ZIP to attach to an issue, without keys, saves or names. The error cards of the queue and of
  Compress games have the same button.
- The page of a game says in an orange line when no sigpatches are found; the game installs all the same. The line
  can be turned off in + and back on in Settings › Installing.
- The library can ask to remove a game. NXDock always asks first, copies its saves as Settings › Saves says, and
  removes the game with its update and DLC.

## 0.9.0-beta.9

- Compress games: a collection of several programs is read with all its contents, so its metadata is never left out

## 0.9.0-beta.8

These changes have not been tested in depth yet, so some bugs may remain.

- Compress games: what a game holds more than once is stored once, wherever it is, also when a game is compressed by
  itself, and the next part is read while the one before is compressed: some games come out about 10 % smaller, and
  faster.
- The name and logo of NXDock are not licensed with the code: a modified version needs its own (TRADEMARKS.md).

## 0.9.0-beta.7

These changes have not been tested in depth yet, so some bugs may remain.

- In the console: deleting many games at once, the question fits on the screen (fewer games listed, "and N more"
  grows) and its long lines end in "…". While it deletes, a bar says which game, of how many, and when it copies its
  saves; each game leaves the list as soon as it is gone. "1 update", not "1 updates", and no "0 DLC".
- In the console: up and down reach the boxes of the question to delete, and A ticks them. While several games are
  deleted, Cancel stops before the next one, never in the middle of one, and says which were not touched.
- In the console, Space and their counts: only games by default; the shortcuts of homebrew on the HOME menu show again
  with "Also show homebrew shortcuts" in the + menu.
- Compress games: the screen goes dark to save battery while it works, as when installing. Both say, small, the game
  at work and, in a queue, which of how many.
- Lists that jump by letter (Compress games, In the console, Space): a stick that drifts a little no longer jumps
  to another letter or moves the chosen game. Only the d-pad and the left stick pushed clearly move them.
- Compress games: the screen stays smooth while it compresses. The work runs below the drawing of the screen, spread
  over the cores.
- Compress games: compress.log keeps the whole round, each game under its title, and says the step where a game
  failed. Before, each game emptied it.
- Compress games: the summary at the end says "2 of 3 games compressed" when one failed, shows that game in grey
  among the covers, and its line in red fits inside the box.
- Compress games: a game whose installed data cannot be read says so, with what to do, whether it is the base game or
  the update, and its code, instead of "The game could not be compressed". Games that fail are in the history too.

## 0.9.0-beta.5

These changes have not been tested in depth yet, so some bugs may remain.

- Compress games: mark one, several or all games (Y, a tap on the box, or the + menu) and compress them in a queue,
  with one question for all. More can be added while it works, and it goes on when you leave the screen.
- Compress games: while it works, a card with the cover, the share done, the speed, the time left, the steps, the size
  before and after, and what comes next. At the end, a summary of what each game saved.
- Compress games: Y marks, as in the other lists; showing every game moved to the + menu.
- Compress games: a game with its update is now also merged and compressed in one pass, in about half the time. In
  0.9.0-beta.4 only games without an update were done in one pass. It measures first only when there is no room to do
  it in one go, and the screen says so.
- Compress games: it says "Getting ready" at the start, instead of "Measuring".

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
- Compress games is no longer experimental: it is always on and has its own tile in More.
- Compress games no longer measures every game first: the list is ready in seconds, and what a game saves shows when
  it is compressed. Its DLC are compressed with it when they take less room.
- Compressing a game is much faster: it is compressed once instead of twice, and a part is written while the next one
  is compressed.
- In the console: the + menu of a game compresses it, and that of the list opens Compress games.
- More shows its tools in three rows of tiles of the same size; the game card, while one is in, comes at the end of
  Your console.
- With the screen off in handheld mode, NXDock no longer draws what cannot be seen, and the queue is no longer copied
  every frame: more of the CPU is left for installing.

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
