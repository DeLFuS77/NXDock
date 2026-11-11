# Using NXDock

A walk through each tab. NXDock works with the buttons and with the touch screen alike: tap a tab, a filter, a cover or
a button of the bottom bar, and drag to scroll.

## The first time

A wizard asks for the language, the theme and the animations, connects to your library app, and asks where to
install by default, what the page of a game ticks, the notices and the saver mode. Every step can be skipped, and it
can be repeated from Settings. It ends with a short walk through the tabs.

Pairing with the library app is explained in the [README](../README.md#pairing-with-the-library-app).

## Library

Your library app's games as a grid of covers.

- **Filters** on top: all, not installed, with an update, with DLC.
- **Search** with the console's keyboard.
- **Long libraries:** the A–Z bar on the right edge (tap it or drag along it), the right stick to jump a letter, and
  − for Options: order by name, size, recently added or not installed first, and Update all.
- **Update all** installs every newer update the library has for what is on the console, each where its game is,
  after a question with the list.

## The page of a game

Its cover, version, format, size, the firmware it needs and what the console has.

- Tick the game, its newest update and the DLC (what is missing comes ticked), choose the SD card or the internal
  memory, and see the room left after.
- **Install** adds them to the queue.
- **Check** reads an installed game and compares every part with its SHA-256; **Repair** then downloads only the
  damaged parts.
- **Another version** puts an older update in place of the installed one, all or nothing.
- X opens the cheats and mods of an installed game.

Before an update, a repair, a removal or another version, the save of the game is copied to your library app. If
the copy fails, NXDock asks before going on.

## Queue

The game being installed, large, with its four stages: network, decompress, write and check. The queue goes on while
you browse.

- At the end: a notice, a soft sound and two gentle pulses of rumble.
- − turns the screen off while it works; any button or touch lights it again.
- The library app shows the queue live and can pause or cancel it; the console asks before cancelling.
- If the console goes off in the middle, the next time NXDock offers to go on where it stopped, or to discard what was
  written. Nothing is registered until an install ends, so the console is never left with half a game.

## Installed

What NXDock installed in this session, which can be undone one by one or all at once, and the history of every
install with its time, speed and result. A failed install says what happened and what to do, with a code to look up.

## More

| Tile | What it does |
|---|---|
| SD or USB disk | Install from files on the SD card or a USB disk (FAT32 or exFAT), split files included. |
| From the computer (MTP) | Install from a computer over MTP: a game installs as it arrives. |
| NS-USBloader | Install from a computer over the USB cable. |
| Shops | Open a shop of the kind of Tinfoil and Ownfoil as another library. |
| Saves | Copy the save of a game to your library app, and restore a copy (the save it replaces is copied first). |
| Space and dumps | Clean leftovers, archive games, move a game between the SD card and the internal memory, plan the queue, dump the game card or an installed game, check the SD card (fake cards, parts that fail, the real speed) and [Compress games](compress-games.md). |
| Cheats and mods | The cheats your library has for the version on the console, and `.zip` mod packages with a switch each. |
| Turn the screen off | The screen goes off while the queue installs; any button or touch lights it again. |

Everything that installs goes to the same queue, all or nothing.

## Settings

Language, theme, animations (full, gentle or none), sounds, notices, the saver mode, the screen off for long queues,
the speed test, sharing what is installed with the library app, the address of the library by hand, and Forget this
library.
