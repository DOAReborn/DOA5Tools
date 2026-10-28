# DOA5Tools

A single ASI plugin for **Dead or Alive 5 Last Round** that brings back lobbies and fixes or improves a few things around them.

## Install

1. Get a fresh or untouched install of the game through Steam if possible.
2. Extract [DOA5Installer](https://github.com/DOAReborn/DOA5Installer/releases/latest) next to `game.exe`.
3. Run `DOA5Installer.exe`, click **Add zips** (`DOA5Tools.zip` and the other DOAReborn zips), then **Install**.
4. Launch the game.

Manual install: extract the zip next to `game.exe`.

### Linux / Steam Deck (Proton)

Install with `python3 doa5installer.py install <zip>...`, then in Steam: right-click the game → **Properties** → **General** → **Launch options**, and paste:

```
WINEDLLOVERRIDES="dinput8=n,b" %command%
```

### Notes

Fully compatible with AutoLink if you're into that.
Turn features on or off in the **EXTRA OPTIONS** menu (main menu), or set them to 0 in the .ini file.
To uninstall, use DOA5Installer.

## Features

**Lobby**
- Adds the missing **Lobby** entry to the Online menu with pretty much every feature available.
- Working invitation system.
- Text chat: Enter (keyboard) or Start (controller) in a lobby.
- Lobby names show in the lobby list, and you can type your own ROOM name.
- CONNECTION setting when creating a lobby.
- The host's Kick button works, and kicked players can't come back to that lobby.

**Rematch**
- If there are only two players queuing in a lobby, the game will automatically show a "rematch" menu at the end of a fight that allows you to play again, right away.
- If one player leaves the menu, or a third player joins the queue, the room goes back to normal.
- Spectators can watch rematches by using spectator mode in lobbies (use the built-in option in the lobby, do not queue).

**Miscellaneous**
- **EXTRA OPTIONS** menu in the main menu: turn DOA5Tools, rollback and DOA5Extra features on or off.
- Shows your real connection type to the others: `[n]` wired, `<n>` Wi-Fi.
- Borderless fullscreen integration (press F11 to cycle between modes).
- Skips the startup logos and intro.
- Runs at your desktop resolution, with ultrawide fixes for unconventional aspect ratios (21:9, 32:9).
- 120/144 fps in fights (off by default, experimental).
- Replay list in rows, with filters by character and player. Archive replays to get past the limit.
- Skip character intros and the victory pose.
- Extra stage slots: the PS4 stages installed with DOA5Installer.
- Console stages in Arcade, Time Attack and Survival.

## Build

32-bit, with [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw) (Windows or Linux). From `src/`:

```
i686-w64-mingw32-gcc -O2 -s -shared -static -Wall -DDOA5TOOLS -o DOA5Tools.asi doa5tools.c lobby.c joinfix.c rematch.c wifiwired.c borderless.c skip.c ultrawide.c replaymenu.c lobbychat.c stageslots.c versiontitre.c fiche.c introoutroskip.c lobbyname.c highfps.c aotfix.c consolerestart.c arcadeconsole.c connfilter.c modoptions.c gamepad.c -liphlpapi -lshell32 -ld3d9 -lgdi32 -lole32 -luuid -lwindowscodecs -lwinmm
```

## Credits

- Ultrawide is a C port of [DOA5LRFix](https://codeberg.org/Lyall/DOA5LRFix) by **Lyall** (MIT, see `third-party/`).
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) by **ThirteenAG** (MIT, see `third-party/`).
- Everything else: DOAReborn Project.
