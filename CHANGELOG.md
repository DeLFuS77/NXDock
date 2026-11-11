# Changelog

## Unreleased

- First build: NXDock starts and shows its version.
- The interface base, drawn on the GPU: library as a grid of covers, the
  queue with the four stages of an install, dark and light themes, the
  console's own fonts and button glyphs. Sample data for now.
- Touch: tabs, filters, covers (tap to select, tap again to open), the queue
  strip and every button of the bottom bar can be tapped; drag to scroll the
  library.
- Network speed test in Settings: the network with 1, 2 and 4 connections at
  once and the SD card apart, without installing anything.
- Connecting without addresses or keys: NXDock finds the library app on the
  local network by itself, shows a code, and once the code is accepted in the
  app it is paired. With "Remember this library" on, the next times it
  connects by itself, also when the address of the app changes. The address
  and the key can still be typed by hand in Settings.
- The real library: the games of the library app with their covers (kept on
  the SD card after the first time), filters with what each shows (all, not
  installed, with an update, with DLC), search with the console's keyboard,
  and what is already on the console read from the system.
- The page of a game: its cover, version, format, size, firmware and what the
  console has; the game, the newest update and every DLC to tick (what is
  missing comes ticked), the SD card or the internal memory, and the space
  left.
- Installing NSP and NSZ to the SD card from the library, on the install core
  of Sphaira (yati):
  - the Install button of a game puts what is ticked in a queue, which goes on
    while the library is browsed; the queue screen and the strip of the
    library show the four stages as they happen: network, decompress, write
    and check;
  - each file comes over several connections at once (as many as the speed
    test found best), in large pieces; a connection that drops is asked again
    from where it stopped;
  - an NSZ travels compressed and its blocks are decompressed on three
    threads; the CPU runs at its fast clock and the console stays awake;
  - all or nothing: the free space and the firmware are checked first, every
    part is checked against its SHA-256 before anything is registered, and a
    failed or cancelled install leaves nothing behind;
- The language of the console: English, Spanish, French, German, Italian,
  Portuguese, Dutch, Russian, Japanese, Korean and Chinese, as the library
  app; English for any other.
- XCI and XCZ install from the library as NSP and NSZ do; the blocks of an
  XCZ are decompressed on three threads too.
- The internal memory: chosen on the page of a game, it always asks first,
  with what goes there and how much room the internal memory has before and
  after; when it does not fit, nothing can be started.
- "Undo all" in the Installed tab: what NXDock installed since it was opened
  can be removed again, one by one or all at once, always after a question.
  Save data is not touched; the game NXDock was opened from is never removed.
- The history of installs, in the Installed tab and on the SD card: date,
  game, where, size, time, speed and result of each, with why a failed one
  failed, a summary on top, and a question before it is cleared.
- "Update all" in the library: every installed game with a newer update in the
  library gets it, each where its game is, after a question with the list.
- "Check" on the page of an installed game: every part on the console is read
  and compared with its SHA-256; "Repair" then downloads only the damaged
  parts from the library and checks them again.
- "Send to console" from the web: what is sent shows on the console with who
  sent it, and nothing is installed until A is pressed; "Not now" tells the
  app it was declined.
- Sharing what is installed with the library app, asked once the first time
  and changed in Settings: the games, updates and DLC of the console and
  where they are; nothing is sent when the answer is no.
- The battery next to the clock, with its %: it fills by the level, turns
  amber below 20 % and red below 10 %, and shows a bolt while charging. With
  little battery and no charger, adding an install to the queue says so first
  (the install is not stopped).
- A QR on the pairing screen, under the code: scanned with a phone it opens
  the library app NXDock found, where the notice to pair shows on any page.
- Soft sounds when moving, choosing, going back and when the queue ends, made
  by NXDock while it plays (nothing recorded is shipped). They are turned off
  in Settings › Sounds.
- When the queue ends: a short notice with what was installed, a soft blue
  glow with a tick, two gentle pulses of HD rumble (in the hands and on
  player 1's controllers) and a sound.
- The cover in focus lifts a little, leans for a moment toward where the
  focus came from and a light passes over it once. Tabs slide in from their
  side, and the page of a game grows out of its cover (and goes back into it).
- Moving fast in a long library: an A–Z bar on the right edge (tap it or drag
  a finger along it), the right stick jumps to the letter before or after, and
  a big letter in the middle says where you are. Holding up or down goes twice
  as fast after a second. − opens Options: the order (A–Z, size, or not
  installed first) and Update all when there is something to update. The
  order and the game in focus are remembered for the next time.
- A first-run wizard, only the first time and repeated from Settings, with
  the soft bossa of the library app's wizard (turned off with −). Every step
  can be skipped: the language (the console's or another one) and the theme
  (light, dark or as the console); connecting to the library and whether to
  share what is installed; where to install by default (SD card, internal
  memory, which always asks first, or ask each time) and what a game's page
  ticks (what is missing, the game only, or the game and its update); the
  notices (end of the queue, sounds, little battery and from which %, updates
  at start); saver mode and the speed test. It ends with a walk of ten
  seconds through Library, Queue and Installed.
- Saver mode: while installing, after a minute without touching anything the
  screen goes dark; any button or a touch lights it again.
- At start, a notice says how many updates the library has for what is
  installed (− Options installs them).
- NSZ and XCZ made with the extra compression of NXPress install as they
  are, also when the library app sends them without turning them back into
  plain NCA first: the parts kept decompressed are compressed again exactly
  as they were, on three threads, and checked against their SHA-256 like
  every other part.
- A warning before the first install of a session when NXDock is not running
  on an emuMMC, or when the console can reach Nintendo's servers (no 90DNS or
  DNS block); the install goes on only after "Install anyway".
- HTTPS with a pinned key: with a library app on an https address, the key of
  its certificate is kept the first time it is linked, and from then on a
  server with another key is refused before anything is sent.
- "Recently added" in Options: the library newest first, from the date the
  library app gives for each game.
- Clear errors: a failed install says what happened and what to do, with its
  code below, in the queue (under the row in focus) and in the history.
- A check before installing: the firmware, the keys of the console and the
  version of Atmosphère against what the queued games ask for. A window shows
  the three lines only when something does not fit, with "Install anyway";
  NXDock never installs firmware or sigpatches.
- A More tab with a tile for each tool.
- Installing without the library app:
  - from the SD card or a USB disk (FAT32 or exFAT): a list of files with the
    places on the left, several can be marked; split files (a folder of parts
    00, 01… or name.nsp.00, .01…) show and install as one;
  - from the computer over MTP: "Install to the SD card" installs a game as it
    arrives, nothing is kept on the card; "Install to the internal memory"
    only after the console says yes; "SD card" to copy files;
  - from the computer over the USB cable with NS-USBloader (the Tinfoil
    protocol).
  Everything goes to the same queue, all or nothing.
- A vault of saves (More › Saves): the games with a save of a user on the
  left, the copies kept in the library app on the right, with their note,
  date, console and version. X copies a save now (with a note that can stay
  empty); A restores a copy, always after a question, and the save it
  replaces goes to the library first ("Before restoring"). R changes the
  user.
- Space and dumps (More › Space): the SD card and the internal memory as
  bars, and:
  - clean leftovers: contents no title uses and tickets of titles that are
    not on the console (archived games keep theirs);
  - archive games: the content goes, the icon stays on the HOME menu;
  - move a game between the SD card and the internal memory (copied,
    checked, then the old copy goes; the internal memory asks first);
  - plan the queue: when it does not fit, the largest games come marked;
  - dump the game card or an installed game to the library app as an NSP,
    in pieces that go on after a cut.
  Every action asks first.
- Shops of the kind of Tinfoil and Ownfoil (More › Shops): an index of
  files, plain JSON or packed with zstd or zlib, opens as another library
  (its name in the top bar; B goes back to the library app). A user and a
  password are typed on the console. An encrypted index is told apart and
  explained: only Tinfoil has its key.
- Cheats and mods (More › Cheats and mods): for a game installed, the
  cheats the library app has for the version on the console, and the .zip
  mod packages of sdmc:/switch/NXDock/mods/<title ID>/ (or any zip in mods/
  with files for it), each with a switch. Mods go to
  atmosphere/contents/<title ID>; what each package writes is written down
  and removing takes away only that. A file of another mod, or one that was
  there before, is never written over.
- The screen off while installing: − on the queue or the tile in More counts
  three seconds and turns the backlight off; any button or touch lights it,
  and the end of the queue lights it by itself. In Settings, "Screen off for
  long queues" (off by default) does it when a queue of 2 GB or more starts.
- MTP with a responder of its own, written from the PTP and MTP
  specifications (no third-party MTP code): file explorers see the install
  folders and the SD card; files over 4 GB come the MTP way
  (SendObjectPropList).
- The planner of the queue marks the games played least lately first; the
  page of an installed game opens its cheats and mods with X.
- More movement and the controller's own symbols: the hint bar and the
  buttons of questions show A, B, X and Y as the diamond with the button lit,
  L, R, ZL and ZR as shoulders, and pulse when pressed; the tiles of More
  come in with a bounce, the one in focus lifts with a gleam and every icon
  moves; list rows slide in and the focus glides; questions grow out of the
  middle; switches and ticks spring; notices rise; bars carry a light at
  their head; the console is drawn with what arrives over MTP or the cable
  flying into it; the screen-off count drains a ring; soft lights move
  behind everything. Settings › Animations: full, gentle or none.
- The first-run wizard asks for the animations next to the theme, offers
  "Turn the screen off for long queues" next to the saver mode, tells those
  without the library app that More installs without it, and its walk ends
  on More.
- Only what is missing is installed: parts the console already has are kept,
  or copied from the other storage.
- Check the SD card (More › Space and dumps): fake cards, parts that fail
  and the real speed, quick or full.
- The save goes to the library before an update, a repair, a removal or
  another version; a copy that fails asks first.
- Before an update, the cheats and mods that stop working with the new
  build are named.
- Another version from the page of a game: an older update in place of the
  installed one, all or nothing.
- The queue seen live in the library app, paused or cancelled from there
  (the console asks first), and a message to the phone when it ends.
- An install cut by the console going off goes on where it stopped, or is
  discarded leaving nothing behind.
- Clean leftovers also takes unfinished installs of other tools, and updates
  and DLC whose game is gone.
- Compress games (experimental, More › Space and dumps): installed games
  compressed with the LZ4 layer of the NCA; a game with its update merged
  with it into one and compressed; its DLC in the same step; newer updates
  merged by themselves; Undo from the library. See docs/compress-games.md.
- Shops on https keep the key of their certificate the first time they open,
  and refuse another one after.
- A new icon: a game card going into the dock.

### Security

- The key of the library or of a shop is sent only to its own server, and a
  library on https is never looked for over plain http.
- XCI and XCZ installs check every NCA against its SHA-256.
- Clean leftovers never removes anything when the console's lists cannot be
  read, and Clean, Archive and Move wait for the queue to end.
- Game files, zips, MTP transfers and saves are read within their limits.
- Compress games and the queue never change the same game at once, and the
  original goes only after the new copy is read back from the SD card.
