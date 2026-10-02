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

**Miscellaneous**
- Shows your real connection type to the others: `[n]` wired, `<n>` Wi-Fi.
- Borderless fullscreen integration (press F11 to cycle between modes).
- Skips the startup logos and intro.
- Ultrawide fixes for unconventional aspect ratios (21:9, 32:9).
- 60FPS mode, menus and offline mode only. Doesn't work without AutoLink.

## Build

32-bit, with [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw) (Windows or Linux). From `src/`:

```
i686-w64-mingw32-gcc -O2 -s -shared -static -Wall -DDOA5TOOLS -o DOA5Tools.asi doa5tools.c lobby.c joinfix.c rematch.c wifiwired.c borderless.c fps60.c skip.c ultrawide.c -liphlpapi -lshell32
```

## Credits

**DOA5Tools** by **FGCsnow**, **BonuStage**, **Buraiden/ice** and **Hajin'**, a **DOAReborn** project.
Lobby, JoinFix, WiFi-Wired, Borderless and 60fps are reworked from our earlier DOA5LR-Salons pack.

Third-party:
- Ultrawide is a C port of [DOA5LRFix](https://codeberg.org/Lyall/DOA5LRFix) by **Lyall** (MIT, see `third-party/`).
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by **ThirteenAG** (MIT, see `third-party/`).
