# System & UI Font: FreeSans

This directory contains the pre-rendered Adafruit-GFX bitmap font (`FreeSans`) used across the KomaBon operating system UI (Main Menu, Settings, Bookshelf, Web Transfer, status bars, and dialogs) via `FontMgr`.

It provides `FreeSans` at 9, 12, 18, and 24pt (both Regular and Bold), generated from GNU FreeFont FreeSans with Latin-1 Supplement coverage (0x20–0xFF), replacing the legacy ASCII-only Adafruit `<Fonts/FreeSans*pt7b.h>` headers.

## License

GNU FreeFont FreeSans is licensed under **GNU GPLv3 with Font Exception**. See [FreeSans.h](FreeSans.h) header comments for details.

## Reader Typography (TrueType Engine)

*Note: Since KomaBon v0.8.0, book reading typography is powered dynamically by the vector TrueType engine (`stb_truetype`) located in `lib/KomaBon_Core/TrueTypeEngine.*` and `lib/KomaBon_Core/TTFonts/`.*

The embedded TrueType vector fonts for reading are:
- **Atkinson Hyperlegible** (SIL OFL 1.1)
- **Merriweather** (SIL OFL 1.1)
- **Literata** (SIL OFL 1.1)
- **Source Serif 4** (SIL OFL 1.1)
- **Gelasio** (SIL OFL 1.1)
- **Open Sans** (SIL OFL 1.1)

Full SIL Open Font License 1.1 text is available in [OFL.txt](OFL.txt).

