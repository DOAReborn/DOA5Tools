# DOA5Tools

A single ASI plugin for **Dead or Alive 5 Last Round** that brings back lobbies and fixes or improves a few things around them.

## Install

1. Get a fresh or untouched install of the game through steam if possible.
2. Extract the zip next to `game.exe`:
   - `dinput8.dll`: ASI Loader
   - `DOA5Tools.asi` + `DOA5Tools.ini`
3. Launch the game.

### Linux / Steam Deck (Proton)

Follow the steps above, then in Steam: right-click the game → **Properties** → **General** → **Launch options**, and paste:

```
WINEDLLOVERRIDES="dinput8=n,b" %command%
```

### Notes

Fully compatible with AutoLink if you're into that.
You can disable any feature by setting its value to 0 in the .ini file.
To uninstall, simply delete these files.

## Features

**Lobby**
- Adds the missing **Lobby** entry to the Online menu with pretty much every feature available.
- Working invitation system, you can also press F7 in a lobby to get your room link copied to your clipboard.

**Rematch**
- If there are only two players queuing in a lobby, the game will automatically show a "rematch" menu at the end of a fight that allows you to play again, right away.
- If one player leaves the menu, or a third player joins the queue, the room goes back to normal.
- Spectators can watch rematches by using spectator mode in lobbies (use the built-in option in the lobby, do not queue).

**Replay Menu**
- Replaces the replay grid (Extra > Spectator > Fight Viewer) with a list in rows: number, player names, character faces, stage, mode, date and duration.
- OK plays a replay in one press. Triangle / Y twice (or the Delete key) deletes it: by default the file is moved to a `REPLY_CORBEILLE` folder next to `REPLY_SAVE` (`Recycle=0` in the `.ini` erases it for good).
- Filters: L1 (or F5) shows only the replays with a given character, R1 (or F6) only those against a given player (press again for the next one, then "All").
- Archive: Square / X twice (or the Insert key) moves a replay out of the game's list to `REPLY_ARCHIVE\<your character>`, with a readable name (players, characters, stage, date). The game keeps at most 100 replays in `REPLY_SAVE`: archived ones do not count. To watch them: R1 = your name, then L1 = the character. Square twice on an archived replay puts it back in the normal list.
- Extra > Spectator opens on Fight Viewer.

**Miscellaneous**
- Shows your real connection type to the others: `[n]` wired, `<n>` Wi-Fi.
- Borderless fullscreen integration (press F11 to cycle between modes).
- Skips the startup logos and intro.
- Ultrawide fixes for unconventional aspect ratios (21:9, 32:9).
- 60FPS mode, menus and offline mode only. Doesn't work without AutoLink.

## Build

32-bit, with [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw) (Windows or Linux). From `src/`:

```
i686-w64-mingw32-gcc -O2 -s -shared -static -Wall -DDOA5TOOLS -o DOA5Tools.asi doa5tools.c lobby.c joinfix.c rematch.c wifiwired.c borderless.c fps60.c skip.c ultrawide.c replaymenu.c -liphlpapi -lshell32 -ld3d9 -lgdi32
```

## Credits

- Ultrawide is a C port of [DOA5LRFix](https://codeberg.org/Lyall/DOA5LRFix) by **Lyall** (MIT, see `third-party/`).
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by **ThirteenAG** (MIT, see `third-party/`).
- Lobby, JoinFix, WiFi-Wired, Borderless and 60fps are reworked from the DOA5LR-Salons pack (FGCsnow & BonuStage).
