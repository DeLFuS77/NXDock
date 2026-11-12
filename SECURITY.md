# Security policy

## Supported versions

Only the latest release gets security fixes.

## Reporting a vulnerability

Please do not open a public issue for a security problem. Report it privately through
[GitHub's private vulnerability reporting](https://github.com/DeLFuS77/NXDock/security/advisories/new), with what an
attacker can do, the steps to reproduce it and the version you tested.

You will get an answer within a few days. Once the fix is released, the report is published as a security advisory,
crediting you unless you prefer otherwise.

## What is in scope

- What NXDock reads from the network, a USB cable or a disk: the answers of the library app and of shops, NSP, NSZ,
  XCI and XCZ containers, MTP and the USB protocol of NS-USBloader, zip packages of mods and saves.
- Pairing with the library app: the code, the key kept on the SD card, the pinned key of an HTTPS address.
- What NXDock writes: installs that must stay all or nothing, the internal memory that is only used after a question,
  mods that never write over other files.
- Updating itself: the signature of a new version and the checksum of `NXDock.nro` that are checked before it is
  put in place.
- What NXDock deletes: the file list, which must never touch the folders of the system, and files deleted after an
  install, only once it is checked.
- The release files and the workflows that build them.

Keys are never part of NXDock's releases. Installing never reads `prod.keys`; only Compress games reads it, keeps it in
memory and never shows or saves it. A report that keys leak through NXDock is in scope.

## Verifying what you download

Every release file comes with a signed provenance that proves it was built by this repository's workflow from the
tagged source (`gh attestation verify <file> -R DeLFuS77/NXDock`), and `SHA256SUMS.txt` is signed with the key of the
project (`minisign -Vm SHA256SUMS.txt -p release.pub`). The signature is made on the maintainer's computer, never on
GitHub.
