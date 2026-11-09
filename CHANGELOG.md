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
  - XCI, XCZ and the internal memory come in a later step.
