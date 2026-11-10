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
